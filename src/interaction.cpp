#include "realm/interaction.hpp"
#include <algorithm>
#include <cmath>
namespace realm {
bool InteractionDialog::open(const HudState::Social& panel,const Entity& player) {
    if(visible()||!panel.enabled||panel.id<0||panel.kind==Kind::none||panel.kind==Kind::hall)return false;
    reset();kind_=panel.kind;player_id_=player.id;guild_=player.guild;
    if(kind_==Kind::leave_guild) { value_=player.name;title_="Renounce Guild";note_="Are you sure you want to quit:\n"+guild_; }
    else if(kind_==Kind::create_guild) { title_="Create a new Guild";note_="Maximum 20 characters. Letters and spaces only.\nCreation costs 1000 fame."; }
    else { title_="Choose a unique account name";note_="Maximum 10 characters. Letters only.\n";
        // This fork's server always charges for a changed name, even if
        // NAME_CHOSEN is false; present the actual server rule in the frame.
        note_+="Changing your name costs 1000 gold.";
    }
    dirty_=true;return true;
}
void InteractionDialog::reset() {
    kind_=Kind::none;player_id_=-1;guild_.clear();value_.clear();error_.clear();title_.clear();note_.clear();pending_=false;pressed_=-1;request_.reset();accepted_name_.reset();dirty_=true;
}
void InteractionDialog::text(std::string_view input) {
    if(!wants_text())return;
    const std::size_t limit=kind_==Kind::create_guild ? 20 : 10;
    for(const unsigned char c:input)if(value_.size()<limit&&((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c==' '&&kind_==Kind::create_guild)))value_+=static_cast<char>(c);
    error_.clear();dirty_=true;
}
void InteractionDialog::backspace() { if(wants_text()&&!value_.empty()) { value_.pop_back();error_.clear();dirty_=true; } }
void InteractionDialog::cancel() { if(!pending_)reset(); }
void InteractionDialog::submit() {
    if(!visible()||pending_)return;
    if(kind_!=Kind::leave_guild&&value_.find_first_not_of(' ')==std::string::npos) { error_="Please enter a name.";dirty_=true;return; }
    request_=Request{kind_,value_};pending_=true;error_.clear();dirty_=true;
}
int InteractionDialog::hit(Vec2 point) {
    if(HudInput::button_at(cancel_button,point))return 0;
    if(HudInput::button_at(submit_button,point))return 1;
    return -1;
}
void InteractionDialog::pointer_down(Vec2 point) { pressed_=pending_ ? -1 : hit(point); }
void InteractionDialog::pointer_up(Vec2 point) {
    const int pressed=std::exchange(pressed_,-1);if(pressed<0||hit(point)!=pressed)return;
    if(pressed==0)cancel();else submit();
}
void InteractionDialog::receive(const net::Event& event) {
    if(event.id==flash::PacketId::MAPINFO||event.id==flash::PacketId::RECONNECT||event.id==flash::PacketId::DEATH) { reset();return; }
    if(!pending_)return;
    bool matched=false,success=false;std::string message;
    if(kind_==Kind::create_guild&&event.id==flash::PacketId::GUILDRESULT) {
        const auto& result=std::get<net::incoming::GuildResult>(event.data);matched=true;success=result.success;message=result.text;
    } else if((kind_==Kind::choose_name||kind_==Kind::change_name)&&event.id==flash::PacketId::NAMERESULT) {
        const auto& result=std::get<net::incoming::NameResult>(event.data);matched=true;success=result.success;message=result.text;
    } else if(kind_==Kind::leave_guild&&event.id==flash::PacketId::TEXT) {
        const auto& result=std::get<net::incoming::Text>(event.data);
        if(result.name=="*Error*") { matched=true;message=result.text; }
    }
    if(!matched)return;
    if(success) {
        const auto name=(kind_==Kind::choose_name||kind_==Kind::change_name) ? std::optional<std::string>{value_} : std::nullopt;
        reset();accepted_name_=name;
    } else { pending_=false;error_=message.empty() ? "The request was rejected." : message.substr(0,512);dirty_=true; }
}
void InteractionDialog::update(const World& world) {
    if(!visible())return;
    const auto* player=world.entity(world.player_id());
    if(!player||player->id!=player_id_) { reset();return; }
    if(kind_==Kind::leave_guild&&player->guild!=guild_)reset();
    if(kind_==Kind::create_guild&&!player->guild.empty())reset();
}
double InteractionDialog::scale(double w,double h) { return std::max(.01,std::min({h/600.0,(w-16)/width,(h-16)/height})); }
Vec2 InteractionDialog::position(double w,double h) { const double s=scale(w,h);return {(w-width*s)/2,(h-height*s)/2}; }
Vec2 InteractionDialog::local(Vec2 point,double w,double h) { const auto p=position(w,h);const double s=scale(w,h);return {(point.x-p.x)/s,(point.y-p.y)/s}; }
void InteractionDialog::rectangle(int x,int y,int w,int h,std::uint32_t color) {
    if(image_.pixel_scale!=1) { ui_rectangle(image_,x,y,w,h,color);return; }
    for(int row=std::max(0,y);row<std::min(height,y+h);++row)for(int col=std::max(0,x);col<std::min(width,x+w);++col) {
        const auto at=static_cast<std::size_t>(row*width+col)*4;
        for(unsigned c=0;c<3;++c)image_.pixels[at+c]=static_cast<unsigned char>((color>>(16-c*8))&255U);
        image_.pixels[at+3]=255;
    }
}
void InteractionDialog::paint(const Image& source,int x,int y) {
    if(image_.pixel_scale!=1) { ui_blit(image_,source,x,y);return; }
    for(int row=0;row<source.height;++row)for(int col=0;col<source.width;++col) {
        const int px=x+col,py=y+row;if(px<0||py<0||px>=width||py>=height)continue;
        const auto src=static_cast<std::size_t>(row*source.width+col)*4,dst=static_cast<std::size_t>(py*width+px)*4;
        const unsigned a=source.pixels[src+3];if(!a)continue;
        for(unsigned c=0;c<3;++c)image_.pixels[dst+c]=static_cast<unsigned char>((source.pixels[src+c]*a+image_.pixels[dst+c]*(255-a)+127)/255);
    }
}
bool InteractionDialog::render(const FontBook& fonts) {
    if(image_.pixel_scale!=fonts.pixel_scale()) { image_=ui_image(width,height,fonts.pixel_scale());dirty_=true; }
    if(!visible()||!dirty_)return false;
    rectangle(0,0,width,height,0xffffff);rectangle(1,1,width-2,height-2,0x363636);rectangle(1,1,width-2,34,0x4d4d4d);
    const auto text=[&](std::string_view value,int size,bool bold,std::uint32_t color,int x,int y,int field=0) {
        const auto b=fonts.rasterize(value,size,bold,true,color,0,0,1,field);
        if(image_.pixel_scale!=1)ui_blit_text(image_,b,x+b.offset.x,y+b.offset.y);
        else paint(b.image,x+static_cast<int>(b.offset.x),y+static_cast<int>(b.offset.y));
    };
    text(title_,18,true,0xffffff,16,5,width-32);
    if(kind_!=Kind::leave_guild) {
        text(kind_==Kind::create_guild ? "Guild Name" : "Name",18,true,0xb3b3b3,20,50);
        rectangle(20,82,400,34,0x454545);rectangle(21,83,398,32,0x222222);
        text(value_,20,false,0xffffff,26,84,388);
    }
    const auto paragraph=[&](std::string_view value,int y,std::uint32_t color,int max_lines) {
        const auto lines=fonts.wrap(value,14,false,width-40);int count=0;
        for(const auto& line:lines) { if(count>=max_lines)break;text(line,14,false,color,20,y+count*18,width-40);++count; }
    };
    paragraph(note_,kind_==Kind::leave_guild ? 62 : 132,0xb3b3b3,4);
    paragraph(error_,218,0xff7777,3);
    if(pending_)text("Waiting for the server...",14,false,0xffffff,20,260);
    for(const auto area:{cancel_button,submit_button}) {
        for(int y=0;y<28;++y) { const int cut=std::max(0,4-std::min(y,27-y));rectangle(static_cast<int>(area.x)+cut,static_cast<int>(area.y)+y,static_cast<int>(area.width)-2*cut,1,pending_ ? 0x777777 : 0xb3b3b3); }
    }
    const std::string left=kind_==Kind::leave_guild ? "No, I'll stay" : "Cancel";
    const std::string right=kind_==Kind::leave_guild ? "Yes, I'll quit" : kind_==Kind::create_guild ? "Create" : "Choose";
    text(left,16,true,0x222222,29,294);text(right,16,true,0x222222,229,294);
    dirty_=false;++rebuilds_;return true;
}
}
