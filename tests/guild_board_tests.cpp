#include "realm/guild_board.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
namespace {
int checks{};
void check(bool value,const char* label) { ++checks;if(!value)throw std::runtime_error(label); }
void click(realm::GuildBoard& b,int i,const realm::FontBook& fonts) {
    const auto r=b.button(i);const auto p=b.position();const auto s=b.scale();const realm::Vec2 at{p.x+(r.x+5)*s,p.y+(r.y+5)*s};
    b.pointer_down(at);b.pointer_up(at);b.update(fonts,1280,720);
}
void focus(realm::GuildBoard& b) { const auto p=b.position();b.pointer_down({p.x+4*b.scale(),p.y+4*b.scale()});b.pointer_up({p.x+4*b.scale(),p.y+4*b.scale()}); }
void wait(realm::GuildBoard& b,const realm::FontBook& fonts) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    while(b.state()==realm::GuildBoard::State::loading||b.state()==realm::GuildBoard::State::saving) {
        b.poll();b.update(fonts,1280,720);
        if(std::chrono::steady_clock::now()>end)throw std::runtime_error("Board HTTP did not finish");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}
int main(int argc,char** argv) {
    try {
        realm::FontBook fonts(argv[1]);fonts.enable_native(argv[2]);fonts.set_pixel_scale(1.2);
        using S=realm::GuildBoard::State;using K=realm::GuildBoard::Key;
        realm::GuildBoard board;
        if(argc>3) {
            const std::string mode=argv[3];std::string password;std::getline(std::cin,password);
            const int rank=std::stoi(argv[6]);
            board.open("127.0.0.1",static_cast<std::uint16_t>(std::stoi(argv[4])),{{"username",argv[5]},{"password",password}},rank);
            if(mode=="stale") {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                const auto before=std::chrono::steady_clock::now();board.close();
                check(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(50),"Close never waits for HTTP");
                board.open("127.0.0.1",static_cast<std::uint16_t>(std::stoi(argv[4])),{{"username",argv[5]},{"password",password}},rank);
                wait(board,fonts);check(board.state()==S::view&&board.text()=="fresh reply","Stale reply ignored; queued reopen loads fresh text");
                board.close();std::cout<<"Native guild board HTTP stale passed ("<<checks<<" checks)\n";return 0;
            }
            check(board.state()==S::loading,"HTTP loading state");check(!board.backdrop(),"Loading hides black backdrop");wait(board,fonts);
            if(mode=="error") { check(board.state()==S::error,"Expected load error");board.close(); }
            else {
                if(board.state()!=S::view)throw std::runtime_error("Raw board load: "+board.status_text());
                click(board,0,fonts);
                if(rank<20)check(board.state()==S::view,"HTTP member cannot edit");
                else {
                    check(board.state()==S::edit,"HTTP officer edits");focus(board);board.key(K::select_all);
                    const std::string value="Guild board round trip\nLiteral + %20 & <tag> Unicode: caf\xc3\xa9\nhttps://example.com/path?x=1&y=2";
                    board.insert(value);click(board,1,fonts);check(board.state()==S::saving,"HTTP saving state");wait(board,fonts);
                    if(mode=="denied"||mode=="retry") {
                        check(board.state()==S::error,"HTTP save error");
                        if(mode=="retry") { // Retry has standard Dialog coordinates.
                            const realm::Vec2 p{640-150.5*1.2+25*1.2,360+30*1.2};
                            board.pointer_down(p);board.pointer_up(p);wait(board,fonts);check(board.state()==S::view,"Retry saves successfully");
                        } else {
                            // Locate Back from the actual standard error dialog image layout.
                            const realm::Vec2 p{640+50*1.2,360+30*1.2};board.pointer_down(p);board.pointer_up(p);
                            check(board.state()==S::edit&&board.text()==value,"Save error Back preserves draft");
                        }
                    } else {
                        check(board.state()==S::view&&board.text()==value,"Native HTTP saves exact returned board");
                        board.close();board.open("127.0.0.1",static_cast<std::uint16_t>(std::stoi(argv[4])),{{"username",argv[5]},{"password",password}},rank);wait(board,fonts);
                        check(board.state()==S::view&&board.text()==value,"Native HTTP readback persists text");
                    }
                }
                board.close();
            }
            std::cout<<"Native guild board HTTP "<<mode<<" passed ("<<checks<<" checks)\n";return 0;
        }
        board.show("Welcome\nhttps://example.com",10);board.update(fonts,1280,720);
        check(board.width()==400,"Short board has no scrollbar");check(board.height()==432,"Source board height");check(board.backdrop(),"View black backdrop");
        click(board,0,fonts);check(board.state()==S::view,"Member edit button absent");
        board.permissions(20);click(board,0,fonts);check(board.state()==S::edit,"Officer edits");check(board.width()==426,"Edit always has scrollbar");
        focus(board);check(board.wants_text(),"Click focuses input");board.key(K::select_all);board.insert("A\xc3\xa9\nB");check(board.text()=="A\xc3\xa9\nB","UTF8 multiline replacement");
        board.key(K::left);board.key(K::backspace);check(board.text()=="A\xc3\xa9" "B","Backspace joins lines");
        board.key(K::backspace);board.key(K::backspace);check(board.text()=="B","Backspace removes complete UTF8 glyph");
        board.key(K::home,false,true);board.insert("abc");board.key(K::left,true);check(board.selection()=="c","Shift selection");board.cut();check(board.text()=="abB","Cut selection");
        board.key(K::end,false,true);board.key(K::enter);board.insert("tail");check(board.text()=="abB\ntail","Enter multiline");
        click(board,0,fonts);check(board.state()==S::view&&board.text()=="Welcome\nhttps://example.com","Cancel discards edits");
        check(!board.update(fonts,1280,720),"Retained board cache");
        click(board,0,fonts);focus(board);board.insert("draft");board.permissions(10);board.update(fonts,1280,720);check(board.state()==S::view,"Demotion exits edit");
        std::string long_text;for(int i=0;i<100;++i)long_text+="A long guild message for the next dungeon.\n";
        board.show(long_text,40);board.update(fonts,1280,720);check(board.width()==426,"Long board scrollbar");board.wheel(-1);check(board.scroll()>0,"Wheel scrolls");
        auto p=board.position();const double s=board.scale();board.pointer_down({p.x+410*s,p.y+200*s});board.pointer_move({p.x+410*s,p.y+500*s});board.pointer_up({p.x+410*s,p.y+500*s});check(board.scroll()>0,"Track paging");
        board.update(fonts,1600,1200);check(board.image().pixel_scale==1.2,"Renderer owns font density");fonts.set_pixel_scale(2);board.show(long_text,40);board.update(fonts,1600,1200);check(board.image().width==884,"Physical density surface");
        board.close();check(!board.visible()&&!board.wants_text(),"Close clears focus");
        fonts.set_pixel_scale(1.2);board.show("https://example.com/"+std::string(80,'a'),20);board.update(fonts,1280,720);
        p=board.position();const realm::Vec2 link{p.x+12,p.y+12};board.pointer_down(link);board.pointer_up(link);
        check(board.take_link()=="https://example.com/"+std::string(80,'a'),"Wrapped link retains full destination");
        board.pointer_up(link);check(!board.take_link(),"Link needs a matching mouse down");
        click(board,0,fonts);focus(board);board.key(K::select_all);board.insert(long_text);board.key(K::end,false,true);board.insert("bottom");
        check(board.scroll()==1,"Typing at final line scrolls caret into view");
        const auto r=board.button(0);p=board.position();board.pointer_down({p.x+(r.x+5)*board.scale(),p.y+(r.y+5)*board.scale()});
        board.update(fonts,1600,1200);board.pointer_up({p.x+(r.x+5)*board.scale(),p.y+(r.y+5)*board.scale()});check(board.state()==S::edit,"Resize cancels stale press");
        realm::Scrollbar scrollbar;scrollbar.indicator(400,400);scrollbar.wheel(-1);scrollbar.tick(1);check(scrollbar.pos()==0,"Fitting scrollbar finite");
        scrollbar.indicator(400,1200);scrollbar.wheel(-1);check(std::abs(scrollbar.pos()-.1)<.0001,"Wheel fifth page");
        scrollbar.down({8,250});check(scrollbar.pos()>.1,"Track page");scrollbar.up();scrollbar.set_pos(0);
        scrollbar.down({8,scrollbar.thumb_y()+4});scrollbar.move({8,600});check(scrollbar.pos()==1,"Thumb clamped drag");scrollbar.focus_lost();scrollbar.move({8,1});check(scrollbar.pos()==1,"Focus loss ends drag");
        scrollbar.down({8,4});scrollbar.tick(.1);check(scrollbar.pos()<1,"Held arrow scrolls");scrollbar.up();const auto at=scrollbar.pos();scrollbar.tick(1);check(scrollbar.pos()==at,"Mouse up ends hold");
        std::cout<<checks<<" guild board and scrollbar checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
