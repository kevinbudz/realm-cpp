#include "realm/startup.hpp"
#include "realm/http.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <random>
#include <regex>
namespace realm {
namespace {
// Signal targets. Dialog actions start at dialog_actions so a modal dialog
// can ignore the screen beneath it.
enum Action : int {
    play_action=1,account_action,legends_action,editor_action,login_action,register_action,back_action,classes_action,
    new_character_action,buy_slot_action,character_action=100,class_action=200,
    dialog_actions=1000,cancel_action=dialog_actions,submit_action,link_action,field_action=1100
};
constexpr std::uint32_t white=0xffffff,gray=11776947,dark_line=5526612;
constexpr std::array<float,4> over_text{1,220.0F/255,133.0F/255,1};
constexpr int frame_width=288,register_width=326;
// The baked HUD portrait holds the 4x outlined sprite in a 34-pixel box 5 pixels in.
constexpr double portrait_inset=5,portrait_content=34;
std::array<float,4> rgba(std::uint32_t color,double alpha=1) {
    return {static_cast<float>((color>>16U)&255U)/255,static_cast<float>((color>>8U)&255U)/255,static_cast<float>(color&255U)/255,static_cast<float>(alpha)};
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read "+path.string());
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
// HudArt records are premultiplied; atlas uploads expect straight alpha.
Image straight(const Image& source) {
    auto image=source;
    for(std::size_t i=0;i<image.pixels.size();i+=4) {
        const unsigned alpha=image.pixels[i+3];
        for(std::size_t c=0;c<3;++c)image.pixels[i+c]=static_cast<unsigned char>(alpha==255 ? image.pixels[i+c] : alpha ? std::min(255U,static_cast<unsigned>(image.pixels[i+c])*256/alpha) : 0);
    }
    return image;
}
// MenuFrame's glow: black, alpha 0/.73/.73/0 at ratios 20/103/173/237.
Image glow_gradient() {
    Image image{1,256,std::vector<unsigned char>(256*4)};
    constexpr double ratios[4]{20,103,173,237},alphas[4]{0,.73,.73,0};
    for(int i=0;i<256;++i) {
        double alpha{};
        for(int k=0;k<3;++k)if(i>=ratios[k]&&i<=ratios[k+1])alpha=alphas[k]+(alphas[k+1]-alphas[k])*(i-ratios[k])/(ratios[k+1]-ratios[k]);
        image.pixels[static_cast<std::size_t>(i)*4+3]=static_cast<unsigned char>(std::lround(alpha*255));
    }
    return image;
}
std::string ordinal(int number) {
    const int ones=number%10,tens=number/10%10;
    return std::to_string(number)+(tens==1 ? "th" : ones==1 ? "st" : ones==2 ? "nd" : ones==3 ? "rd" : "th");
}
std::string time_difference(std::int64_t time) {
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto dt=now-time;
    if(dt<=0)return "now";
    if(dt<60)return std::to_string(dt)+" secs";
    if(dt<3600)return std::to_string(dt/60)+" mins";
    if(dt<86400)return std::to_string(dt/3600)+" hours";
    return std::to_string(dt/86400)+" days";
}
std::int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
void pop_utf8(std::string& value) {
    while(!value.empty()) { const auto c=static_cast<unsigned char>(value.back());value.pop_back();if((c&0xC0U)!=0x80U)break; }
}
}
Startup::Startup(const FontBook& fonts,const HudArt* art,const Catalog& catalog,const std::filesystem::path& root,StartupOptions options)
    : fonts_(fonts),art_(art),catalog_(catalog),root_(root),options_(std::move(options)),sheet_{sheet_size,sheet_size,std::vector<unsigned char>(static_cast<std::size_t>(sheet_size)*sheet_.width*4)} {
    if(fonts_.empty())throw std::runtime_error("Menus need a font pack; bake one with tools/bake_fonts.py");
    if(options_.account_file.empty())options_.account_file=account_path();
    logo_=load_png(root_/"src/kabam/rotmg/ui/view/TitleView_Logo.png");normalize_flash_png(logo_);
    background_map_=read_file(root_/"src/kabam/rotmg/ui/view/components/MapBackground_EMBEDDED_BACKGROUNDMAP.dat");
    // ObjectLibrary.playerChars_: Player definitions in their XML order.
    for(const auto& object:catalog_.objects())if(object.class_name=="Player")player_types_.push_back(object.type);
    if(player_types_.empty())throw std::runtime_error("No Player definitions in the object catalog");
    if(art_)skins_=std::make_unique<SkinChooser>(catalog_,fonts_,*art_);
    std::random_device random;new_character_type_=player_types_[random()%player_types_.size()];
    // WebLoadAccountTask: verify saved credentials, otherwise continue as a guest.
    saved_=load_account(options_.account_file);
    account_.char_use=saved_.char_use;
    if(!saved_.username.empty())start_task(Task::verify_saved,"/account/verify",{{"username",saved_.username},{"password",saved_.password}});
    else start_task(Task::char_list,"/char/list",credentials());
}
Startup::~Startup() { if(request_.valid())request_.wait(); }
void Startup::load_background(World& world) {
    constexpr int border=10;
    world.load_jm_text(background_map_,border);
    background_width_=world.width()-2*border;
    std::random_device random;
    background_x_=border;background_y_=border+static_cast<int>((world.height()-2*border)*std::uniform_real_distribution<double>(0,1)(random));
}
net::FormFields Startup::credentials() const { return {{"username",account_.username},{"password",account_.password}}; }
void Startup::start_task(Task task,const std::string& path,net::FormFields fields) {
    if(task_!=Task::none)return;
    task_=task;
    request_=std::async(std::launch::async,[host=options_.host,port=options_.app_port,path,fields=std::move(fields)] { return net::http_post(host,port,path,fields); });
}
void Startup::update(std::int32_t time,double dt,double width,double height) {
    time_=time;width_=width;height_=height;
    if(sheet_.pixel_scale!=fonts_.pixel_scale()) { sheet_=ui_image(sheet_size,sheet_size,fonts_.pixel_scale());sheet_used_=0;dirty_=true; }
    if(background()&&background_width_>0) {
        background_x_+=dt/1000;
        if(background_x_>background_width_+10)background_x_-=background_width_;
    }
    if(task_!=Task::none&&request_.valid()&&request_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        std::string response,error;
        try { response=request_.get(); } catch(const std::exception& e) { error=e.what(); }
        finish_task(response,error);
    }
    if(retry_at_>=0&&time>=retry_at_) {
        retry_at_=-1;
        if(retry_task_==Task::verify_saved)start_task(Task::verify_saved,"/account/verify",{{"username",saved_.username},{"password",saved_.password}});
        else start_task(Task::char_list,"/char/list",credentials());
    }
    const bool caret=wants_text()&&time/500%2==0;
    if(caret!=caret_) { caret_=caret;dirty_=true; }
    if(skins_) {
        skins_->poll();
        if(auto purchase=skins_->take_purchase();purchase&&purchase->username==account_.username&&list_) {
            if(!purchase->error.empty()) {
                if(screen_==Screen::game)skin_error_=purchase->error;
                else if(screen_!=Screen::skins)skins_->purchase_error(purchase->error);
            } else if(std::find(list_->owned_skins.begin(),list_->owned_skins.end(),purchase->id)==list_->owned_skins.end()) { list_->owned_skins.push_back(purchase->id);list_->credits=std::max(0,list_->credits-1000);dirty_=true; }
        }
    }
    if(screen_==Screen::skins&&skins_) {
        selected_skins_[new_character_type_]=skins_->selected();
        if(skins_->update(width,height,time)) { const auto r=skins_->change();skin_texture_.update_region(skins_->image(),r.x,r.y,r.width,r.height); }
        if(list_) { if(list_->credits!=skins_->data().credits)dirty_=true;list_->owned_skins=skins_->data().owned_skins;list_->credits=skins_->data().credits; }
    }
    if(skins_&&skins_->visible()&&screen_!=Screen::skins&&skins_->modal_input())if(skins_->update(width,height,time)) { const auto r=skins_->change();skin_texture_.update_region(skins_->image(),r.x,r.y,r.width,r.height); }
    if(dirty_)rebuild();
}
void Startup::finish_task(const std::string& response,const std::string& error) {
    const auto task=std::exchange(task_,Task::none);
    const auto failure=error.empty() ? app_error(response) : std::optional<std::string>(error);
    dirty_=true;
    switch(task) {
    case Task::verify_saved:
        // An unreachable server is retried; a rejected login continues as a guest.
        if(!error.empty()) { loading_text_="Load error, retrying";retry_task_=Task::verify_saved;retry_at_=time_+1000;break; }
        if(!failure) { account_.username=saved_.username;account_.password=saved_.password; }
        start_task(Task::char_list,"/char/list",credentials());
        break;
    case Task::char_list:
        if(failure) {
            loading_text_="Load error, retrying";
            // GetCharListTask: rejected credentials are cleared and the guest list is requested at once.
            if(*failure=="Account credentials not valid") {
                account_={};selected_skins_.clear();if(skins_)skins_->close();
                try { save_account(options_.account_file,account_); } catch(const std::exception&) {}
                start_task(Task::char_list,"/char/list",credentials());
            } else { retry_task_=Task::char_list;retry_at_=time_+1000; }
            break;
        }
        try {
            list_=parse_char_list(response);sort_characters(*list_,account_.char_use);
        } catch(const std::exception&) { loading_text_="Load error, retrying";retry_task_=Task::char_list;retry_at_=time_+1000;break; }
        invalidated_=false;loading_text_="Loading...";scroll_=0;
        if(screen_==Screen::account_loading||screen_==Screen::loading)screen_=pending_screen_;
        if(screen_==Screen::skins&&skins_) { skins_->endpoint(options_.host,options_.app_port,credentials());skins_->open(new_character_type_,selected_skins_[new_character_type_],*list_,true); }
        break;
    case Task::login:
    case Task::reg:
        if(failure) {
            if(!fields_.empty())fields_[task==Task::login ? 1 : 0].error=*failure;
            break;
        }
        selected_skins_.clear();if(skins_)skins_->close();
        account_.username=fields_.at(0).value;account_.password=fields_.at(1).value;
        try { save_account(options_.account_file,account_); } catch(const std::exception& e) { open_dialog(Dialog::message,e.what());break; }
        dialog_=Dialog::none;invalidated_=true;
        with_valid_data(screen_==Screen::characters||screen_==Screen::classes||screen_==Screen::skins ? screen_ : Screen::title);
        break;
    case Task::none: break;
    }
}
void Startup::with_valid_data(Screen next) {
    dirty_=true;
    if(!invalidated_) { screen_=next;return; }
    // SetScreenWithValidDataCommand: LoadingScreen until GetCharListTask completes.
    pending_screen_=next;screen_=Screen::loading;loading_text_="Loading...";
    start_task(Task::char_list,"/char/list",credentials());
}
void Startup::play(const SavedCharacter* character,int class_type) {
    // The server rejects HELLO without credentials, so a guest signs in first.
    if(!account_.registered()) { open_dialog(Dialog::login);return; }
    if(!list_)return;
    SessionOptions session;
    session.host=options_.host;session.port=options_.game_port;session.build_version=options_.build_version;
    session.username=account_.username;session.password=account_.password;
    session.game_id=-1;
    session.create_character=character==nullptr;
    session.character_id=character ? character->id : list_->next_char_id;
    if(!character) { session.class_type=static_cast<std::uint16_t>(class_type);if(screen_==Screen::skins&&skins_&&skins_->owned(skins_->selected()))session.skin_type=static_cast<std::uint16_t>(skins_->selected()); }
    // PlayGameCommand.recordCharacterUseInSharedObject.
    account_.char_use[session.character_id]=now_ms();
    try { save_account(options_.account_file,account_); } catch(const std::exception&) {}
    launch_=std::move(session);
    screen_=Screen::entering;loading_text_="Loading...";dirty_=true;
}
void Startup::entered_game() { screen_=Screen::game;dialog_=Dialog::none;hovered_=pressed_=-1;dirty_=true; }
void Startup::renamed_account(const std::string& name) {
    account_.username=name;saved_.username=name;if(list_)list_->name=name;
    try { save_account(options_.account_file,account_); }
    catch(const std::exception& e) { account_save_error_="Your new login name is "+name+". Could not save it: "+e.what(); }
    invalidated_=true;
}
void Startup::left_game(const std::string& message) {
    invalidated_=true;hovered_=pressed_=-1;
    with_valid_data(Screen::characters);
    const auto notice=message+(message.empty()||account_save_error_.empty() ? "" : "\n")+account_save_error_;account_save_error_.clear();
    if(!notice.empty())open_dialog(Dialog::message,notice);
}
void Startup::open_dialog(Dialog dialog,std::string message) {
    dialog_=dialog;fields_.clear();focus_=0;dirty_=true;hovered_=pressed_=-1;
    if(dialog==Dialog::login) { dialog_title_="Sign in";fields_={{"Username",{},{},false},{"Password",{},{},true}}; }
    else if(dialog==Dialog::reg) { dialog_title_="Register";fields_={{"Username",{},{},false},{"Password",{},{},true},{"Retype Password",{},{},true}}; }
    else { dialog_title_="Realm C++";dialog_message_=std::move(message); }
}
void Startup::submit_dialog() {
    if(task_!=Task::none)return;
    if(dialog_==Dialog::message) { dialog_=Dialog::none;dirty_=true;return; }
    static const std::regex username(R"(^[a-zA-Z0-9]+$)");
    auto& name=fields_.at(0);auto& password=fields_.at(1);
    bool valid=true;
    if(!std::regex_match(name.value,username)||name.value.size()>12) { name.error="Invalid username";valid=false; }
    if(password.value.size()<9) { password.error="Invalid password";valid=false; }
    if(dialog_==Dialog::reg&&fields_.at(2).value!=password.value) { fields_.at(2).error="The password did not match";valid=false; }
    dirty_=true;
    if(!valid)return;
    if(dialog_==Dialog::login)start_task(Task::login,"/account/verify",{{"username",name.value},{"password",password.value}});
    else start_task(Task::reg,"/account/register",{{"newUsername",name.value},{"newPassword",password.value}});
}
void Startup::activate(int action) {
    dirty_=true;
    if(action>=field_action) { focus_=static_cast<std::size_t>(action-field_action);return; }
    switch(action) {
    case cancel_action: dialog_=Dialog::none;return;
    case submit_action: submit_dialog();return;
    case link_action: open_dialog(dialog_==Dialog::login ? Dialog::reg : Dialog::login);return;
    case play_action:
        if(screen_==Screen::skins) { if(list_&&list_->available_slots()>0)play(nullptr,new_character_type_);return; }
        if(screen_==Screen::title)with_valid_data(Screen::characters);
        else if(list_&&!list_->characters.empty())play(&list_->characters.front(),0);
        else screen_=Screen::classes;
        return;
    case account_action:
        if(account_.registered())open_dialog(Dialog::message,"Logged in as "+account_.username+". Use \"log out\" to switch accounts.");
        else open_dialog(Dialog::login);
        return;
    case legends_action: open_dialog(Dialog::message,"The legends screen is not available in the C++ client yet.");return;
    case editor_action: open_dialog(Dialog::message,"The map editor is not available in the C++ client yet.");return;
    case buy_slot_action: open_dialog(Dialog::message,"Buying character slots is not available in the C++ client yet.");return;
    case login_action:
        if(!account_.registered()) { open_dialog(Dialog::login);return; }
        // WebLogoutCommand: forget the account and reload the guest data.
        account_={};selected_skins_.clear();if(skins_)skins_->close();
        try { save_account(options_.account_file,account_); } catch(const std::exception&) {}
        invalidated_=true;with_valid_data(Screen::title);
        return;
    case register_action: open_dialog(Dialog::reg);return;
    case back_action: if(screen_==Screen::skins) { screen_=Screen::classes;if(skins_)skins_->close(); }else screen_=screen_==Screen::classes ? Screen::characters : Screen::title;return;
    case classes_action: screen_=Screen::classes;return;
    case new_character_action: with_valid_data(Screen::classes);return;
    default: break;
    }
    if(action>=class_action&&action<class_action+static_cast<int>(player_types_.size())) {
        new_character_type_=player_types_[static_cast<std::size_t>(action-class_action)];
        if(skins_&&list_) { screen_=Screen::skins;skins_->endpoint(options_.host,options_.app_port,credentials());skins_->open(new_character_type_,selected_skins_[new_character_type_],*list_,true); }
    }
    else if(list_&&action>=character_action&&action<character_action+static_cast<int>(list_->characters.size()))play(&list_->characters[static_cast<std::size_t>(action-character_action)],0);
}
void Startup::pointer_move(Vec2 position) { if(skins_&&skins_->visible()&&skins_->modal_input()) { skins_->pointer_move(position);hovered_=-1;return; }hovered_=hit(position);if(screen_==Screen::skins&&dialog_==Dialog::none&&skins_)skins_->pointer_move(position); }
void Startup::pointer_down(Vec2 position) {
    if(skins_&&skins_->visible()&&skins_->modal_input()) { skins_->pointer_down(position);pressed_=-1;return; }
    if(screen_==Screen::skins&&dialog_==Dialog::none&&skins_) { skins_->pointer_down(position);if(skins_->modal_input()) { pressed_=-1;return; } }
    pressed_=hit(position);
    // CreateNewCharacterRect and text fields respond to MOUSE_DOWN.
    if(pressed_==new_character_action||pressed_>=field_action) { activate(pressed_);pressed_=-1; }
}
void Startup::pointer_up(Vec2 position) {
    if(skins_&&skins_->visible()&&skins_->modal_input()) { skins_->pointer_up(position);pressed_=-1;return; }
    if(screen_==Screen::skins&&dialog_==Dialog::none&&skins_) { const bool modal=skins_->modal_input();skins_->pointer_up(position);if(modal) { pressed_=-1;return; } }
    const int target=hit(position);
    if(target>=0&&target==pressed_)activate(target);
    pressed_=-1;hovered_=hit(position);
}
void Startup::wheel(double steps) {
    if(skins_&&skins_->visible()&&skins_->modal_input())return;
    if(screen_==Screen::skins&&dialog_==Dialog::none&&skins_) { skins_->wheel(steps);return; }
    if(screen_!=Screen::characters||dialog_!=Dialog::none||list_height_<=400)return;
    scroll_=std::clamp(scroll_-steps*30,0.0,list_height_-400);dirty_=true;
}
void Startup::text(std::string_view utf8) {
    if(skins_&&skins_->visible()&&skins_->modal_input())return;
    if(!wants_text()||focus_>=fields_.size())return;
    auto& field=fields_[focus_];
    if(field.value.size()+utf8.size()>64)return;
    field.value+=utf8;field.error.clear();dirty_=true;
}
bool Startup::key(StartupKey key) {
    if(!menus())return false;
    if(skins_&&skins_->visible()&&skins_->modal_input())return true;
    if(dialog_==Dialog::none)return key!=StartupKey::escape||screen_!=Screen::title;
    dirty_=true;
    switch(key) {
    case StartupKey::escape: if(task_==Task::none||dialog_==Dialog::message)dialog_=Dialog::none;break;
    case StartupKey::enter: submit_dialog();break;
    case StartupKey::tab: if(!fields_.empty())focus_=(focus_+1)%fields_.size();break;
    case StartupKey::backspace: if(wants_text()&&focus_<fields_.size()) { pop_utf8(fields_[focus_].value);fields_[focus_].error.clear(); } break;
    }
    return true;
}
void Startup::reset_sheet() {
    // Stale pixels in the gaps would bleed into filtered edges of the next screen's elements.
    std::fill_n(sheet_.pixels.begin(),static_cast<std::ptrdiff_t>(sheet_used_)*sheet_.width*4,static_cast<unsigned char>(0));
    sheet_x_=sheet_y_=1;sheet_row_=0;
}
HudRegion Startup::store(const Image& image) {
    if(image.width+2>sheet_.width||image.height+2>sheet_.height)throw std::runtime_error("Menu element exceeds sheet size");
    if(sheet_x_+image.width+1>sheet_.width) { sheet_x_=1;sheet_y_+=sheet_row_+1;sheet_row_=0; }
    if(sheet_y_+image.height+1>sheet_.height)throw std::runtime_error("Menu sheet is full");
    const HudRegion region{sheet_x_,sheet_y_,image.width,image.height};
    for(int y=0;y<image.height;++y)
        std::copy_n(image.pixels.begin()+static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y)*static_cast<std::size_t>(image.width)*4),static_cast<std::ptrdiff_t>(image.width)*4,
                    sheet_.pixels.begin()+static_cast<std::ptrdiff_t>((static_cast<std::size_t>(region.y+y)*static_cast<std::size_t>(sheet_.width)+static_cast<std::size_t>(region.x))*4));
    sheet_x_+=image.width+1;sheet_row_=std::max(sheet_row_,image.height);
    return region;
}
Startup::Element& Startup::add_solid(double x,double y,double w,double h,std::uint32_t color,double alpha,int group) {
    Element e;e.solid=true;e.position={x,y};e.size={w,h};e.color=rgba(color,alpha);e.group=group;
    return elements_.emplace_back(e);
}
Vec2 Startup::add_text(std::string_view value,int size,bool bold,std::uint32_t color,double x,double y,int group,bool shadow) {
    const auto bitmap=fonts_.rasterize(value,size,bold,shadow,color,x-std::floor(x),0);
    Element e;e.source=store(bitmap.image);e.group=group;
    e.text=value;e.text_origin={x,y};e.font_size=size;e.font_bold=bold;e.font_shadow=shadow;
    e.position={std::round(x+bitmap.offset.x),std::floor(y)+bitmap.offset.y};e.size={bitmap.image.width/bitmap.image.pixel_scale,bitmap.image.height/bitmap.image.pixel_scale};
    if(group>=0)e.over=over_text;
    elements_.push_back(e);
    return {bitmap.width,bitmap.height};
}
Vec2 Startup::add_button(std::string_view label,int size,double x,double y,int action,bool pulse,bool center) {
    // TitleMenuOption: lower-case bold white text that tints on hover.
    std::string text(label);std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto field=fonts_.layout(text,size,true);
    if(center)x=(800-field.width)/2+x;
    const auto first=elements_.size();
    add_text(text,size,true,white,x,y,action);
    auto& hit_area=add_solid(x,y,field.width,field.height,0,0,action);hit_area.hit=true;
    if(pulse)for(auto i=first;i<elements_.size();++i) { elements_[i].pulse=true;elements_[i].pulse_center={x+field.width/2,y+field.height/2}; }
    return {field.width,field.height};
}
void Startup::add_image(const Image& image,double x,double y,double scale,int group) {
    Element e;
    if(sheet_.pixel_scale!=1) {
        auto pixels=ui_image(image.width*scale,image.height*scale,sheet_.pixel_scale);
        auto source=image;source.pixel_scale=1/scale;ui_blit(pixels,source,0,0);
        e.source=store(pixels);
    } else e.source=store(image);
    e.position={x,y};e.size={image.width*scale,image.height*scale};e.group=group;
    elements_.push_back(e);
}
void Startup::add_window_text(std::string_view value,int size,bool bold,std::uint32_t color,Vec2 anchor,Vec2 offset,Vec2 pixels,bool align_right,bool align_bottom,int action) {
    const auto field=fonts_.layout(value,size,bold);
    if(align_right)offset.x-=field.width;
    if(align_bottom)offset.y-=field.height;
    const auto first=elements_.size();
    add_text(value,size,bold,color,offset.x,offset.y,action);
    if(action>=0) { auto& h=add_solid(offset.x,offset.y,field.width,field.height,0,0,action);h.hit=true; }
    for(auto i=first;i<elements_.size();++i) { elements_[i].anchor=Anchor::window;elements_[i].anchor_fraction=anchor;elements_[i].pixels=pixels; }
}
void Startup::menu_frame(bool background) {
    const auto window=[&](Element& e) { e.anchor=Anchor::window;e.full_width=true; };
    // MenuBackground dims the scrolling map; MenuFrame adds the glow band and the bar.
    if(background)window(add_solid(0,0,0,600,0x2B2B2B,.8));
    Element glow;glow.source=store(glow_gradient());glow.position={0,508};glow.size={0,80};window(glow);elements_.push_back(glow);
    window(add_solid(0,524,0,52,0x545454));
}
void Startup::loading_screen(std::string_view text) {
    const auto width=fonts_.layout(text,30,true).width;
    add_text(text,30,true,white,(800-width)/2,526);
}
void Startup::account_header() {
    // AccountScreen anchors WebAccountInfoView 10 pixels from the right edge,
    // laid out right to left by addAndAlignHorizontally.
    struct Part { std::string text;int action; };
    std::vector<Part> parts;
    if(account_.registered())parts={{"logged in as "+account_.username+" - ",-1},{"log out",login_action}};
    else parts={{"guest account - ",-1},{"register",register_action},{" - ",-1},{"log in",login_action}};
    double x{};
    for(auto part=parts.rbegin();part!=parts.rend();++part) {
        x-=fonts_.layout(part->text,18,part->action>=0).width;
        const auto first=elements_.size();
        add_text(part->text,18,part->action>=0,part->action>=0 ? white : gray,x,2,part->action);
        if(part->action>=0) { auto& h=add_solid(x,2,fonts_.layout(part->text,18,true).width,fonts_.layout(part->text,18,true).height,0,0,part->action);h.hit=true; }
        for(auto i=first;i<elements_.size();++i) { elements_[i].anchor=Anchor::window;elements_[i].anchor_fraction={1,0};elements_[i].pixels={-10,0}; }
    }
}
void Startup::title_screen() {
    menu_frame(true);
    add_image(logo_,(800-logo_.width)/2.0,83);
    add_button("PLAY",36,0,520,play_action,true,true);
    add_button("ACCOUNT",22,-94,532,account_action,false,true);
    add_button("LEGENDS",22,96,532,legends_action,false,true);
    add_button("EDITOR",22,8,532,editor_action);
    add_window_text("RotMG "+options_.build_version,12,false,8355711,{0,1},{0,0},{0,0},false,true);
    add_window_text("\xC2\xA9 2010, 2011 by Wild Shadow Studios, Inc.",12,false,8355711,{1,1},{0,0},{0,0},true,true);
    account_header();
}
void Startup::character_screen() {
    menu_frame(true);
    auto& divider=add_solid(0,104,0,2,dark_line);divider.anchor=Anchor::window;divider.full_width=true;
    const std::string name=list_&&!list_->name.empty() ? list_->name : "Undefined";
    add_text(name,22,true,gray,(800-fonts_.layout(name,22,true).width)/2,24);
    add_text("Characters",18,true,gray,10,79);
    add_text("News",18,true,gray,410,79);
    add_solid(399,107,2,419,dark_line);
    if(list_) {
        // NewsList: NewsLine rows of 52 pixels, 4 apart.
        for(std::size_t i=0;i<list_->news.size();++i) {
            const auto& item=list_->news[i];const double y=112+4+static_cast<double>(i)*56;
            add_text(item.title,18,false,gray,400+61,y);
            add_text(item.tagline,14,false,gray,400+61,y+24);
            const auto when=time_difference(item.date);
            add_text(when,16,false,gray,400+388-fonts_.layout(when,16,false).width,y);
        }
        // CharacterRectList inside the 400-pixel scrolled CharacterList.
        double y=4;
        const auto rect=[&](std::uint32_t color,std::uint32_t over,int action) {
            const double top=112+y-scroll_;
            const bool visible=top>=112&&top+59<=112+430;
            if(visible) {
                auto& box=add_solid(10,top,356,59,color,1,action);box.over=rgba(over);
                if(action>=0) { auto& h=add_solid(10,top,356,59,0,0,action);h.hit=true; }
            }
            y+=59+4;
            return visible ? top : -1.0;
        };
        const auto portrait=[&](int type,double x,double top,int group) {
            // makeIcon(template,100): the 5x redrawn sprite, centered in the rect's height.
            if(art_)add_image(straight(art_->image("portrait"+std::to_string(type))),x-portrait_inset*1.25,top+(59-portrait_content*1.25)/2-portrait_inset*1.25,1.25,group);
        };
        for(std::size_t i=0;i<list_->characters.size();++i) {
            const auto& c=list_->characters[i];const int action=character_action+static_cast<int>(i);
            const double top=rect(6052956,8355711,action);if(top<0)continue;
            portrait(catalog_.portrait_type(c.object_type,c.skin),14,top,action);
            const auto* def=c.object_type>0&&c.object_type<=65535 ? catalog_.object(static_cast<std::uint16_t>(c.object_type)) : nullptr;
            add_text((def ? def->id : "Unknown")+" "+std::to_string(c.level),18,true,white,10+58,top);
            const auto best=list_->best_fame.find(c.object_type);
            if(const int next=next_star_fame(best==list_->best_fame.end() ? 0 : best->second,c.fame);next>0)
                add_text("Class Quest: "+std::to_string(c.fame)+" of "+std::to_string(next)+" Fame",14,false,gray,10+58+14,top+24);
        }
        for(int i=0;i<list_->available_slots();++i) {
            const double top=rect(5526612,7829367,new_character_action);if(top<0)continue;
            portrait(new_character_type_,14,top,new_character_action);
            add_text("New Character",18,true,white,10+58,top);
            const int remaining=static_cast<int>(player_types_.size())*5-list_->stars();
            if(remaining>0)add_text(std::to_string(remaining)+" Class quests not yet completed",14,false,gray,10+58+14,top+24);
        }
        if(const double top=rect(2039583,4342338,buy_slot_action);top>=0) {
            add_text("Buy "+ordinal(list_->max_num_chars+1)+" Character Slot",18,true,white,10+58,top+2);
            add_text("2000",18,false,white,10+356-43-fonts_.layout("2000",18,false).width,top+15);
        }
        list_height_=y;
    }
    add_button("play",36,0,520,play_action,true,true);
    add_button("back",22,-94,532,back_action,false,true);
    add_button("classes",22,96,532,classes_action,false,true);
    account_header();
}
void Startup::class_screen() {
    menu_frame(true);
    add_button("back",36,0,524,back_action,false,true);
    // NewCharacterScreen: five CharacterBoxes per row, 140 pixels apart.
    constexpr double box=100;
    for(std::size_t i=0;i<player_types_.size();++i) {
        const int action=class_action+static_cast<int>(i);
        const double x=50+140*static_cast<double>(i%5)+70-box/2,y=88+140*static_cast<double>(i/5);
        auto& border=add_solid(x,y,box,box,0x7F7F7F,1,action);border.over=rgba(0x999999);
        auto& fill=add_solid(x+2,y+2,box-4,box-4,0x363636,1,action);fill.over=rgba(0x414141);
        if(art_) {
            // SavedCharacter.getImage: a 6x redrawn sprite centered at the top of the box.
            add_image(straight(art_->image("portrait"+std::to_string(player_types_[i]))),x+(box-portrait_content*1.5)/2-portrait_inset*1.5,y+10-portrait_inset*1.5,1.5,action);
            elements_.back().over={1.2F,1.2F,1.2F,1};
        }
        const auto* def=catalog_.object(static_cast<std::uint16_t>(player_types_[i]));
        const std::string name=def ? def->id : "Unknown";
        add_text(name,14,true,white,x+(box-fonts_.layout(name,14,true).width)/2,y+74,action);
        elements_.back().over={1.2F,1.2F,1.2F,1};
        auto& h=add_solid(x,y,box,box,0,0,action);h.hit=true;
    }
    account_header();
}
void Startup::skin_screen() {
    menu_frame(true);
    add_solid(0,104,800,2,dark_line).full_width=true;
    add_solid(345,105,2,421,dark_line);
    const auto first=elements_.size();
    const bool available=list_&&list_->available_slots()>0;
    add_button("play",36,0,520,available ? play_action : -1,false,true);
    if(!available)for(auto i=first;i<elements_.size();++i) { elements_[i].color=rgba(0x363636);elements_[i].over={}; }
    add_button("back",22,30,534,back_action);account_header();
}
void Startup::dialog_frame() {
    const bool message=dialog_==Dialog::message;
    const int w=dialog_==Dialog::reg ? register_width : frame_width;
    std::vector<std::string> lines;
    if(message)lines=fonts_.wrap(dialog_message_,12,false,w-34);
    // Frame: each TextInputField is 88 tall from h-60, links and plain text 20 from h-66.
    double h=100;std::vector<double> field_y,line_y;
    for(std::size_t i=0;i<fields_.size();++i) { field_y.push_back(h-60);h+=88; }
    const double link_y=h-66;if(!message)h+=20;
    for(std::size_t i=0;i<lines.size();++i) { line_y.push_back(h-66);h+=20; }
    const double ox=(800-(w-6))/2.0,oy=(600-h)/2;
    add_solid(ox-7,oy-7,w+2,h+2,white);
    add_solid(ox-6,oy-6,w,h,3552822);
    add_solid(ox-6,oy-6,w,32,5066061);
    add_text(dialog_title_,12,false,gray,ox+5,oy);
    for(std::size_t i=0;i<fields_.size();++i) {
        const auto& f=fields_[i];const double y=oy+field_y[i];const int action=field_action+static_cast<int>(i);
        add_text(f.label,18,true,gray,ox+17,y);
        add_solid(ox+16,y+29,240,32,4539717);
        add_solid(ox+17,y+30,238,30,3355443);
        std::string shown=f.password ? std::string(f.value.size(),'*') : f.value;
        if(i==focus_&&caret_)shown+='|';
        if(!shown.empty())add_text(shown,20,false,gray,ox+17+6,y+30,-1,false);
        if(!f.error.empty())add_text(f.error,12,false,16549442,ox+17,y+62);
        auto& hit_area=add_solid(ox+17,y+30,238,30,0,0,action);hit_area.hit=true;
    }
    if(!message) {
        const std::string link=dialog_==Dialog::login ? "New user?  Click here to Register" : "Already registered?  Click here to sign in";
        const auto field=fonts_.layout(link,12,false);
        add_text(link,12,false,white,ox+17,oy+link_y,link_action);
        auto& hit_area=add_solid(ox+17,oy+link_y,field.width,field.height,0,0,link_action);hit_area.hit=true;
    }
    for(std::size_t i=0;i<lines.size();++i)add_text(lines[i],12,false,white,ox+17,oy+line_y[i]);
    const auto button=[&](const std::string& label,double x,int action) {
        const auto field=fonts_.layout(label,18,true);
        if(x<0)x=w-field.width-26;
        add_text(label,18,true,task_!=Task::none ? gray : white,ox+x,oy+h-52,action);
        auto& hit_area=add_solid(ox+x,oy+h-52,field.width,field.height,0,0,action);hit_area.hit=true;
    };
    if(!message)button("Cancel",109,cancel_action);
    button(message ? "Ok" : dialog_==Dialog::login ? "Sign in" : "Register",-1,submit_action);
}
void Startup::rebuild() {
    dirty_=false;reset_sheet();elements_.clear();
    switch(screen_) {
    case Screen::account_loading: menu_frame(false);loading_screen(loading_text_);break;
    case Screen::loading: case Screen::entering: menu_frame(true);loading_screen(loading_text_);break;
    case Screen::title: title_screen();break;
    case Screen::characters: character_screen();break;
    case Screen::classes: class_screen();break;
    case Screen::skins: skin_screen();break;
    case Screen::game: break;
    }
    dialog_begin_=elements_.size();
    if(dialog_!=Dialog::none&&screen_!=Screen::game)dialog_frame();
    const int used=std::min(sheet_.height,sheet_y_+sheet_row_+1);
    uploads_.push_back({0,0,sheet_.width,std::max(used,sheet_used_)});
    sheet_used_=used;
}
Vec2 Startup::to_window(const Element& e,Vec2 p) const {
    const double scale=height_/600;
    if(e.anchor==Anchor::content)return {(width_-800*scale)/2+p.x*scale,(height_-600*scale)/2+p.y*scale};
    return {e.anchor_fraction.x*width_+e.pixels.x+p.x*scale,e.anchor_fraction.y*height_+e.pixels.y+p.y*scale};
}
int Startup::hit(Vec2 p) const {
    if(!menus())return -1;
    const double scale=height_/600;
    for(auto e=elements_.rbegin();e!=elements_.rend();++e) {
        if(!e->hit||(dialog_!=Dialog::none&&e->group<dialog_actions))continue;
        auto origin=to_window(*e,e->position);Vec2 size{e->size.x*scale,e->size.y*scale};
        if(e->full_width) { origin.x=0;size.x=width_; }
        if(p.x>=origin.x&&p.y>=origin.y&&p.x<origin.x+size.x&&p.y<origin.y+size.y)return e->group;
    }
    return -1;
}
void Startup::build(const SurfaceTexture& texture,std::int32_t time,std::vector<Quad>& out) {
    if(!menus())return;
    const double scale=height_/600;
    // TitleMenuOption pulse: 1.05 + 0.05 sin(t / 200) about the field center.
    const double pulse=1.05+.05*std::sin(time/200.0);
    bool skin_drawn=false;std::size_t element_index=0;
    const bool skin_visible=skins_&&skins_->visible()&&(screen_==Screen::skins||skins_->modal_input());
    for(const auto& e:elements_) {
        if(element_index++==dialog_begin_&&skin_visible&&!skins_->modal_input()) { out.push_back(skin_texture_.quad({0,0},{width_,0},{0,height_}));skin_drawn=true; }
        if(e.hit||(e.solid&&e.color[3]<=0))continue;
        auto origin=to_window(e,e.position);Vec2 size{e.size.x*scale,e.size.y*scale};
        if(e.full_width) { origin.x=0;size.x=width_; }
        if(e.pulse) {
            const auto pivot=to_window(e,e.pulse_center);
            origin={pivot.x+(origin.x-pivot.x)*pulse,pivot.y+(origin.y-pivot.y)*pulse};size={size.x*pulse,size.y*pulse};
        }
        const bool over=e.group>=0&&e.group==hovered_&&e.over[3]>0;
        if(e.pulse&&!e.text.empty()) {
            const auto pivot=to_window(e,e.pulse_center),field=to_window(e,e.text_origin);
            const double density=scale*pulse;
            const double padding=e.font_shadow ? 8 : 0;
            const Vec2 bitmap_origin{pivot.x+(field.x-pivot.x)*pulse-padding*density,
                                    pivot.y+(field.y-pivot.y)*pulse-padding*density};
            const Vec2 at{std::floor(bitmap_origin.x),std::floor(bitmap_origin.y)};
            const Vec2 phase{bitmap_origin.x-at.x,bitmap_origin.y-at.y};
            // The quad stays at native texel size. Fractional motion belongs in
            // glyph coverage, so neither texture filtering nor pixel snapping
            // introduces stepping as the pulse grows and shrinks.
            const auto bitmap=fonts_.rasterize_at_scale(e.text,e.font_size,density,e.font_bold,e.font_shadow,0xffffff,phase);
            const auto logical=fonts_.layout(e.text,e.font_size,e.font_bold);
            pulse_texture_.reserve(static_cast<int>(std::ceil((logical.width+padding*2)*scale*1.1))+1,
                                   static_cast<int>(std::ceil((logical.height+padding*2)*scale*1.1))+1);
            pulse_texture_.update(bitmap.image);
            auto q=pulse_texture_.quad(at,{static_cast<double>(bitmap.image.width),0},{0,static_cast<double>(bitmap.image.height)});
            q.multiply=over ? e.over : e.color;out.push_back(q);continue;
        }
        Quad q;
        if(e.solid) {
            q.origin={static_cast<float>(origin.x),static_cast<float>(origin.y)};q.axis_x={static_cast<float>(size.x),0};q.axis_y={0,static_cast<float>(size.y)};
            q.kind=2;q.multiply=over ? e.over : e.color;
        } else {
            // The sheet is rasterized at the window density and sampled without filtering, so a quad whose size or
            // origin is a fraction of a pixel off its texels duplicates or drops texel rows and shifts the content.
            if(!e.pulse&&!e.full_width&&std::abs(size.x-e.source.width)<1&&std::abs(size.y-e.source.height)<1) {
                origin={std::round(origin.x),std::round(origin.y)};size={static_cast<double>(e.source.width),static_cast<double>(e.source.height)};
            }
            q=texture.quad(origin,{size.x,0},{0,size.y},e.source.x,e.source.y,e.source.width,e.source.height);
            q.kind=5;q.multiply=over ? e.over : e.color;
        }
        out.push_back(q);
    }
    if(skin_visible&&!skin_drawn)out.push_back(skin_texture_.quad({0,0},{width_,0},{0,height_}));
}
}
