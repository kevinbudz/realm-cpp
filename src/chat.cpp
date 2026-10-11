#include "realm/chat.hpp"
#include "realm/ui_draw.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace realm {
namespace {
// TextBox layout constants.
constexpr double indent=20,min_line_height=16,icon_line_height=16.5,block_spacing=4,first_width=596,next_width=576;
// FTE line ascent for 14-pixel MyriadProBoldCFF, and where a TextField-style
// bitmap's baseline sits relative to that line top.
constexpr double text_baseline_offset=-3,text_gutter=2,icon_y=-2;
// ElementFormats colors.
constexpr std::uint32_t normal_color=0xffffff,server_color=0xffff00,client_color=0x0000ff,help_color=0xff5b05,
    error_color=0xff0000,admin_color=0xffff00,enemy_color=0xffa800,player_color=0x00ff00,separator_color=0x363636,
    tell_color=0x00f0ff,guild_color=0xa6ff5d;
std::size_t code_points(std::string_view text) {
    return static_cast<std::size_t>(std::count_if(text.begin(),text.end(),[](char c) { return (static_cast<unsigned char>(c)&0xc0U)!=0x80U; }));
}
}

void Chat::push(ChatLine line) {
    lines_.push_back(std::move(line));
    if(lines_.size()>capacity)lines_.pop_front();
    scroll_=std::min(scroll_,lines_.size());
    ++revision_;
}

void Chat::add(std::string name,std::string text,std::int32_t time) { push({time,std::move(name),-1,"",false,std::move(text)}); }

void Chat::add(const net::incoming::Text& message,std::string_view own_name,std::int32_t time) {
    const bool from_me=!message.recipient.empty()&&message.name==own_name,to_me=message.recipient==own_name;
    push({time,message.name,message.stars,message.recipient,to_me,message.text});
    const auto remember=[&](const std::string& name) { if(tellers_.empty()||tellers_.back()!=name)tellers_.push_back(name); };
    if(to_me)remember(message.name);
    else if(message.recipient!=guild_chat_name&&from_me)remember(message.recipient);
    // Bound the tell history like the log.
    if(tellers_.size()>capacity)tellers_.erase(tellers_.begin());
}

std::vector<std::size_t> Chat::visible(std::int32_t time) const {
    std::vector<std::size_t> result;
    const auto count=lines_.size();
    const std::size_t start=count>1+scroll_+max_lines ? count-1-scroll_-max_lines : 0;
    const std::size_t end=std::min(count,start+max_lines+1);
    for(std::size_t i=start;i<end;++i)if(show_max_||time<=lines_[i].time+line_time)result.push_back(i);
    return result;
}

void Chat::scroll_up() {
    if(!show_max_)show_max_=true;
    else scroll_=lines_.size()>line_scroll ? std::min(lines_.size()-line_scroll,scroll_+line_scroll) : 0;
    ++revision_;
}

void Chat::scroll_down() {
    if(scroll_==0)show_max_=false;
    else scroll_=scroll_>line_scroll ? scroll_-line_scroll : 0;
    ++revision_;
}

void Chat::open(std::optional<std::string_view> text) {
    typing_=true;current_teller_=tellers_.size();
    if(text)input_=*text;
    ++revision_;
}

void Chat::tell(bool backward) {
    if(!typing_)open();
    input_="/tell ";
    if(!tellers_.empty()) {
        // currentTeller_ starts past the newest entry; plain Tab steps toward
        // newer tellers, Shift+Tab toward older ones, both clamped.
        auto current=static_cast<std::ptrdiff_t>(current_teller_)+(backward ? 1 : -1);
        current=std::clamp<std::ptrdiff_t>(current,0,static_cast<std::ptrdiff_t>(tellers_.size())-1);
        current_teller_=static_cast<std::size_t>(current);
        input_+=tellers_[current_teller_]+" ";
    }
    ++revision_;
}

void Chat::text(std::string_view utf8) {
    if(!typing_)return;
    for(std::size_t i=0;i<utf8.size();) {
        const auto lead=static_cast<unsigned char>(utf8[i]);
        const std::size_t length=lead<0x80 ? 1 : lead>=0xf0 ? 4 : lead>=0xe0 ? 3 : 2;
        if(i+length>utf8.size()||code_points(input_)>=max_chars)break;
        if(lead>=0x20&&lead!=0x7f)input_.append(utf8.substr(i,length));
        i+=length;
    }
    ++revision_;
}

void Chat::backspace() {
    if(!typing_||input_.empty())return;
    auto end=input_.size()-1;
    while(end>0&&(static_cast<unsigned char>(input_[end])&0xc0U)==0x80U)--end;
    input_.erase(end);++revision_;
}

std::optional<std::string> Chat::submit() {
    if(!typing_)return {};
    std::optional<std::string> result;
    if(!input_.empty())result=std::exchange(input_,std::string{});
    close();
    return result;
}

std::vector<ChatView::Row> ChatView::layout(const ChatLine& line) const {
    // TextBoxLine.getTextBlock.
    std::string name=line.name;
    std::uint32_t name_color=player_color,text_color=normal_color;
    if(line.name==server_chat_name) { name.clear();text_color=server_color; }
    else if(line.name==client_chat_name) { name.clear();text_color=client_color; }
    else if(line.name==help_chat_name) { name.clear();text_color=help_color; }
    else if(line.name==error_chat_name) { name.clear();text_color=error_color; }
    if(!line.name.empty()&&line.name.front()=='#') { name=line.name.substr(1);name_color=enemy_color; }
    if(!line.name.empty()&&line.name.front()=='@') { name=line.name.substr(1);name_color=text_color=admin_color; }
    int icon=line.stars>=0 ? std::min(line.stars,99) : -1;
    struct Token { std::string text;std::uint32_t color{};int icon{-1}; };
    std::vector<Token> tokens;
    const auto words=[&](std::string_view text,std::uint32_t color) {
        // A line may break after any space.
        std::size_t begin=0;
        while(begin<text.size()) {
            auto end=text.find(' ',begin);
            end=end==std::string_view::npos ? text.size() : text.find_first_not_of(' ',end);
            if(end==std::string_view::npos)end=text.size();
            tokens.push_back({std::string(text.substr(begin,end-begin)),color});begin=end;
        }
    };
    if(line.recipient==guild_chat_name)text_color=guild_color;
    else if(!line.recipient.empty()) {
        text_color=tell_color;
        if(!line.to_me) { words("To: ",normal_color);name=line.recipient;icon=-1; }
    }
    if(icon>=0&&art_&&art_->contains("chatstar"+std::to_string(icon)))tokens.push_back({"",0,icon});
    if(!name.empty()) { tokens.push_back({"<"+name+">",name_color});tokens.push_back({" ",separator_color}); }
    words(line.text,text_color);
    // Greedy word wrapping into a 596-pixel first line and indented 576-pixel rest.
    std::vector<Row> rows(1);double x=0;
    const auto width_of=[&](const Token& token) {
        // GraphicElement width: the rank icon plus two pixels.
        return token.icon>=0 ? 18.0 : fonts_.measure(token.text,14,true);
    };
    const auto trimmed=[&](const Token& token) {
        if(token.icon>=0)return 18.0;
        const auto end=token.text.find_last_not_of(' ');
        return end==std::string::npos ? 0.0 : fonts_.measure(std::string_view(token.text).substr(0,end+1),14,true);
    };
    for(auto token:tokens) {
        const double limit=rows.size()==1 ? first_width : next_width;
        if(x>0&&x+trimmed(token)>limit) { rows.emplace_back();x=0; }
        // A word wider than a whole line breaks between characters.
        while(token.icon<0&&trimmed(token)>(rows.size()==1 ? first_width : next_width)-x) {
            const double room=(rows.size()==1 ? first_width : next_width)-x;
            std::size_t fit=0;
            for(std::size_t next=1;next<=token.text.size();++next) {
                if(next<token.text.size()&&(static_cast<unsigned char>(token.text[next])&0xc0U)==0x80U)continue;
                if(fonts_.measure(std::string_view(token.text).substr(0,next),14,true)>room)break;
                fit=next;
            }
            if(fit==0&&x==0)fit=std::min<std::size_t>(1,token.text.size());
            if(fit>0) {
                Token head{token.text.substr(0,fit),token.color};
                rows.back().pieces.push_back({head.text,head.color,-1,x,width_of(head)});
                token.text.erase(0,fit);
            }
            rows.emplace_back();x=0;
        }
        const double token_width=width_of(token);
        auto& pieces=rows.back().pieces;
        if(token.icon<0&&!pieces.empty()&&pieces.back().icon<0&&pieces.back().color==token.color) {
            pieces.back().text+=token.text;pieces.back().width=fonts_.measure(pieces.back().text,14,true);
        } else pieces.push_back({token.text,token.color,token.icon,x,token_width});
        x=pieces.back().x+pieces.back().width;
    }
    for(auto& row:rows) {
        row.height=min_line_height;
        for(const auto& piece:row.pieces)if(piece.icon>=0)row.height=icon_line_height;
    }
    return rows;
}

bool ChatView::update(const Chat& chat,std::int32_t time) {
    const auto shown=chat.visible(time);
    const double scale=fonts_.pixel_scale();
    if(shown==shown_&&chat.revision()==revision_&&scale==scale_)return false;
    shown_=shown;revision_=chat.revision();scale_=scale;
    std::vector<std::vector<Row>> blocks;double text_height=0;
    for(const auto index:shown) {
        blocks.push_back(layout(chat.lines()[index]));
        if(text_height!=0)text_height+=block_spacing;
        for(const auto& row:blocks.back())text_height+=row.height;
    }
    // TextBox.placeTextField.
    const double sprite_y=chat.typing() ? height-8-18-text_height : height-4-text_height;
    if(blocks.empty()&&!chat.typing()) { surface_height_=0;++rebuilds_;return true; }
    top_=std::floor(sprite_y-4);surface_height_=height-top_;
    image_=ui_image(width,surface_height_,scale);
    double y=sprite_y-top_;
    for(std::size_t b=0;b<blocks.size();++b) {
        if(b)y+=block_spacing;
        for(std::size_t r=0;r<blocks[b].size();++r) {
            const auto& row=blocks[b][r];const double left=2+(r==0 ? 0 : indent);
            for(const auto& piece:row.pieces) {
                if(piece.icon>=0) {
                    // The rank icon's baked origin is at (8,8). Its GraphicElement sits on
                    // the baseline and TextBoxLine lowers the sprite by 3.
                    ui_blit(image_,art_->image("chatstar"+std::to_string(piece.icon)),left+piece.x-8,y+icon_y-8,true,true);
                    continue;
                }
                const auto bitmap=fonts_.rasterize(piece.text,14,true,false,piece.color);
                ui_blit_text(image_,bitmap,left+piece.x-text_gutter+bitmap.offset.x,y+text_baseline_offset+bitmap.offset.y,false,true);
            }
            y+=row.height;
        }
    }
    if(chat.typing()) {
        // The 598x18 input TextField with its one-pixel white border.
        const double field_y=height-20-top_;
        ui_rectangle(image_,0,field_y,599,1,0xffffff,true);ui_rectangle(image_,0,field_y+18,599,1,0xffffff,true);
        ui_rectangle(image_,0,field_y,1,19,0xffffff,true);ui_rectangle(image_,598,field_y,1,19,0xffffff,true);
        const auto text_width=fonts_.measure(chat.input(),14,true);
        // TextField scrolls horizontally to keep the caret in view.
        const double scroll=std::max(0.0,text_width+4-598);
        if(!chat.input().empty()) {
            const auto bitmap=fonts_.rasterize(chat.input(),14,true,false,0xffffff);
            ui_blit_text(image_,bitmap,bitmap.offset.x-scroll,field_y+bitmap.offset.y,false,true,1,1,field_y+1,597,17);
        }
        ui_rectangle(image_,std::min(596.0,2+text_width-scroll),field_y+2,1,14,0xffffff,true);
    }
    glow();
    ++rebuilds_;
    return true;
}

void ChatView::glow() {
    // GlowFilter(0,1,3,3,2,1): a 3-pixel box blur of coverage at double
    // strength, composited as black beneath the premultiplied text.
    const int w=image_.width,h=image_.height,r=std::max(1,static_cast<int>(std::lround(image_.pixel_scale)));
    const unsigned taps=static_cast<unsigned>(2*r+1);
    std::vector<unsigned> horizontal(static_cast<std::size_t>(w)*static_cast<std::size_t>(h));
    const auto alpha=[&](int x,int y) { return static_cast<unsigned>(image_.pixels[(static_cast<std::size_t>(y)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x))*4+3]); };
    for(int y=0;y<h;++y) {
        unsigned sum{};
        for(int x=-r;x<w+r;++x) {
            if(x+r<w)sum+=alpha(x+r,y);
            if(x-r-1>=0)sum-=alpha(x-r-1,y);
            if(x>=0&&x<w)horizontal[static_cast<std::size_t>(y)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x)]=sum;
        }
    }
    std::vector<unsigned> column(static_cast<std::size_t>(h));
    for(int x=0;x<w;++x) {
        unsigned sum{};
        for(int y=-r;y<h+r;++y) {
            if(y+r<h)sum+=horizontal[static_cast<std::size_t>(y+r)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x)];
            if(y-r-1>=0)sum-=horizontal[static_cast<std::size_t>(y-r-1)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x)];
            if(y>=0&&y<h)column[static_cast<std::size_t>(y)]=sum;
        }
        for(int y=0;y<h;++y) {
            const unsigned blurred=std::min(255U,column[static_cast<std::size_t>(y)]*2/(taps*taps));
            auto* pixel=&image_.pixels[(static_cast<std::size_t>(y)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x))*4];
            const unsigned a=pixel[3],combined=a+blurred*(255U-a)/255U;
            // Black adds coverage only; convert premultiplied color to straight alpha.
            for(int c=0;c<3;++c)pixel[c]=static_cast<unsigned char>(combined ? std::min(255U,pixel[c]*255U/combined) : 0U);
            pixel[3]=static_cast<unsigned char>(combined);
        }
    }
}

namespace {
int texture_height(const ObjectDef& definition) {
    const auto& texture=definition.texture;
    if(texture.animated>=0&&static_cast<std::size_t>(texture.animated)<flash::animated.size())return flash::animated[static_cast<std::size_t>(texture.animated)].height;
    if(texture.sheet>=0&&static_cast<std::size_t>(texture.sheet)<flash::sheets.size())return flash::sheets[static_cast<std::size_t>(texture.sheet)].height;
    return 8;
}
}

void SpeechBalloons::add(const World& world,const net::incoming::Text& message) {
    if(message.object_id<0)return;
    const auto* object=world.entity(message.object_id);if(!object)return;
    const auto* definition=world.catalog().object(object->type);
    Balloon balloon;balloon.object_id=message.object_id;balloon.lifetime=message.bubble_time*1000;balloon.text=message.text;
    // Normal, enemy, tell and guild color triples from onText.
    if(message.recipient==guild_chat_name) { balloon.background=0x3e8a00;balloon.outline=0xa6ff5d;balloon.color=0xd3f7cc; }
    else if(!message.recipient.empty()) { balloon.background=0x260ab6;balloon.outline=0x00f0ff;balloon.color=0xd3ccf7; }
    else if(definition&&definition->enemy) { balloon.background=0x561f1c;balloon.outline=0xfc8642;balloon.color=0xcdc0bf; }
    else { balloon.background=0xe1dfdc;balloon.outline=0xffffff;balloon.color=0x545454; }
    if(definition)balloon.texture_height=texture_height(*definition);
    std::erase_if(balloons_,[&](const Balloon& b) { return b.object_id==balloon.object_id; });
    if(balloons_.size()>=capacity)balloons_.erase(balloons_.begin());
    // Reuse the lowest free GPU slot.
    std::vector<bool> used(capacity);for(const auto& b:balloons_)used[b.slot]=true;
    balloon.slot=static_cast<std::size_t>(std::find(used.begin(),used.end(),false)-used.begin());
    balloons_.push_back(std::move(balloon));
}

void SpeechBalloons::update(const World& world,const FontBook& fonts,std::int32_t time) {
    std::erase_if(balloons_,[&](const Balloon& b) { return !world.entity(b.object_id)||(b.start>=0&&time-b.start>b.lifetime); });
    for(auto& balloon:balloons_) {
        if(balloon.start<0)balloon.start=time;
        if(balloon.image.pixels.empty()||balloon.image.pixel_scale!=fonts.pixel_scale())render(balloon,fonts);
    }
}

void SpeechBalloons::render(Balloon& balloon,const FontBook& fonts) {
    // A 150-pixel wordWrap TextField with autoSize LEFT, regular 14-pixel text.
    std::string joined;
    const auto lines=fonts.wrap(balloon.text,14,false,150);
    for(const auto& line:lines) { if(!joined.empty())joined+='\n';joined+=line; }
    const auto run=fonts.layout(joined,14);
    // textHeight is 16.8 pixels per regular 14-pixel Myriad line; autoSize adds 4.
    const double w=std::floor(run.width),h=16.8*static_cast<double>(std::max<std::size_t>(1,lines.size()))+4;
    balloon.field_width=w;balloon.field_height=h;balloon.full_height=std::floor(h+26);
    // drawCutEdgeRect(-6,-6,w+12,h+12,4) with the tail spliced into its bottom edge.
    const double l=-6,t=-6,r=w+6,b=h+6;
    const std::array<Vec2,11> path{{{l+4,t},{r-4,t},{r,t+4},{r,b-4},{r-4,b},{w/2+8,h+6},{w/2,h+18},{w/2-8,h+6},{l+4,b},{l,b-4},{l,t+4}}};
    balloon.image=ui_image(w+margin*2,h+18+margin*2,fonts.pixel_scale());
    auto& image=balloon.image;const double s=image.pixel_scale;
    // Shift the path into the image's margin.
    std::array<Vec2,11> shifted{};
    for(std::size_t i=0;i<path.size();++i)shifted[i]={path[i].x+margin,path[i].y+margin};
    ui_path(image,shifted,balloon.background,balloon.outline,2);
    if(!joined.empty()) {
        const auto bitmap=fonts.rasterize(joined,14,false,false,balloon.color);
        ui_blit_text(image,bitmap,margin+bitmap.offset.x,margin+bitmap.offset.y,false,true);
    }
    // SpeechBalloon's DropShadowFilter(0,0,0,1,16,16).
    ui_shadow(image,8,image.width/s,image.height/s);
    balloon.revision=++revisions_;
}

Vec2 SpeechBalloons::position(const Balloon& balloon,Vec2 object_screen,int object_size,double scale) {
    // offset_ = (-w/2, -height - texture.height*size/100*5 - 2), truncated to whole pixels.
    const double x=std::trunc(object_screen.x/scale-balloon.field_width/2);
    const double y=std::trunc(object_screen.y/scale-balloon.full_height-balloon.texture_height*(object_size/100.0)*5-2);
    return {(x-margin)*scale,(y-margin)*scale};
}
}
