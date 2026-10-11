#include "realm/font.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>3||(argc==3&&std::string_view(argv[2])!="--stress"))throw std::runtime_error("Usage: realm_font_inputs FONT_DIRECTORY [--stress]");
        const std::filesystem::path root=std::filesystem::absolute(argv[1]);
        std::ifstream input(root/"tasks.json");const auto tasks=nlohmann::json::parse(input);
        realm::FontBook fonts(root);
        if(argc==3) {
            for(const auto& style:tasks.at("fonts")) {
                if(style.at("shadow").get<bool>())continue;
                for(const auto& value:style.at("codes")) {
                    const auto code=value.get<std::uint32_t>();std::string text;
                    if(code<128)text.push_back(static_cast<char>(code));
                    else if(code<2048) { text.push_back(static_cast<char>(0xc0U|(code>>6U)));text.push_back(static_cast<char>(0x80U|(code&63U))); }
                    else { text.push_back(static_cast<char>(0xe0U|(code>>12U)));text.push_back(static_cast<char>(0x80U|((code>>6U)&63U)));text.push_back(static_cast<char>(0x80U|(code&63U))); }
                    (void)fonts.rasterize(text,style.at("size"),style.at("bold"),false,0xffffff,0,style.value("vertical_phase",0.0));
                }
            }
            if(fonts.cache_entries()>4096||fonts.cache_bytes()>8*1024*1024||fonts.cache_evictions()==0)throw std::runtime_error("Font stress did not prove bounded cache eviction");
        }
        for(const auto& style:tasks.at("fonts")) {
            std::size_t index{};
            for(const auto& sample:tasks.at("samples")) {
                const auto bitmap=fonts.rasterize(sample.get<std::string>(),style.at("size"),style.at("bold"),style.at("shadow"),0xffffff,0,style.value("vertical_phase",0.0));
                realm::Image canvas{800,600,std::vector<unsigned char>(800*600*4)};
                const int ox=8+static_cast<int>(bitmap.offset.x),oy=8+static_cast<int>(bitmap.offset.y);
                for(int y=0;y<bitmap.image.height;++y)for(int x=0;x<bitmap.image.width;++x) {
                    if(ox+x<0||oy+y<0||ox+x>=800||oy+y>=600)continue;
                    const auto src=(static_cast<std::size_t>(y)*static_cast<std::size_t>(bitmap.image.width)+static_cast<std::size_t>(x))*4;
                    const auto dst=(static_cast<std::size_t>(oy+y)*800+static_cast<std::size_t>(ox+x))*4;
                    std::copy_n(bitmap.image.pixels.data()+src,4,canvas.pixels.data()+dst);
                }
                realm::save_png(root/"native-transparent"/style.at("id").get<std::string>()/(std::to_string(index++)+".png"),canvas);
            }
        }
        std::cout<<"Font cache "<<fonts.cache_entries()<<" glyphs, "<<fonts.cache_bytes()<<" bytes, "<<fonts.cache_evictions()<<" evictions\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
