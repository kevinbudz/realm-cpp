#include "realm/telemetry.hpp"
#include "realm/font.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <zlib.h>
#include <bit>
#include <cmath>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
namespace realm {
namespace {
// Vertical phases lie on Flash's twentieth-pixel grid.
int style(int size,bool bold,bool shadow,double vertical_phase=0) {
    const auto twips=std::lround(vertical_phase*20);
    if(!(vertical_phase>=0&&vertical_phase<1)||std::abs(vertical_phase*20-static_cast<double>(twips))>1e-6)throw std::runtime_error("Unsupported text vertical phase");
    return (size*4+(bold?2:0)+(shadow?1:0))*20+static_cast<int>(twips);
}
// SimpleText glyph coverage is the union of glyph paths: edge coverage is
// added before quantization instead of painting one bitmap over another.
void composite(Image& target,const Image& bitmap,int gx,int gy,int padding,double right,double bottom,std::uint32_t color) {
    for(int y=0;y<bitmap.height;++y)for(int x=0;x<bitmap.width;++x) {
        const int tx=gx+x,ty=gy+y;
        if(tx<0 || tx>=right || ty<0 || ty>=bottom)continue;
        const auto src=(static_cast<std::size_t>(y)*static_cast<std::size_t>(bitmap.width)+static_cast<std::size_t>(x))*4;
        const auto dst=(static_cast<std::size_t>(ty+padding)*static_cast<std::size_t>(target.width)+static_cast<std::size_t>(tx+padding))*4;
        const unsigned clip=static_cast<unsigned>(std::lround(std::clamp(right-tx,0.0,1.0)*4))*64U;
        const unsigned alpha=std::min<unsigned>(bitmap.pixels[src+3],clip ? clip-1 : 0);
        if(!alpha)continue;
        const unsigned previous_alpha=target.pixels[dst+3];
        const auto combined=static_cast<unsigned char>(std::min(255U,alpha+previous_alpha+(previous_alpha ? 1U : 0U)));
        target.pixels[dst+3]=combined;
        for(unsigned c=0;c<3;++c)target.pixels[dst+c]=static_cast<unsigned char>(((color>>(16U-8U*c))&255U)*(combined+1U)>>8U);
    }
}
// Flash's blurX=blurY=4 filter has five centered taps weighted 1,2,2,2,1.
// It truncates after the horizontal pass, then again after the vertical
// pass. Alpha compositing uses a 256 divisor. Filters act on the complete
// clipped TextField: a per-glyph shadow would darken the preceding glyph.
void finish(Image& image,bool shadow,unsigned shadow_strength,double blur=4) {
    if(shadow&&(image.pixel_scale!=1||blur!=4)) {
        const int w=image.width,h=image.height,radius=std::max(1,static_cast<int>(std::round(blur/2*image.pixel_scale)));
        std::vector<unsigned> horizontal(static_cast<std::size_t>(w)*static_cast<std::size_t>(h));
        const unsigned taps=static_cast<unsigned>(radius*2+1);
        for(int y=0;y<h;++y) {
            unsigned sum{};
            for(int x=-radius;x<w;++x) {
                if(x+radius<w)sum+=image.pixels[(static_cast<std::size_t>(y)*w+static_cast<std::size_t>(x+radius))*4+3];
                if(x-radius-1>=0)sum-=image.pixels[(static_cast<std::size_t>(y)*w+static_cast<std::size_t>(x-radius-1))*4+3];
                if(x>=0)horizontal[static_cast<std::size_t>(y)*w+static_cast<std::size_t>(x)]=sum/taps;
            }
        }
        for(int x=0;x<w;++x) {
            unsigned sum{};
            for(int y=-radius;y<h;++y) {
                if(y+radius<h)sum+=horizontal[static_cast<std::size_t>(y+radius)*w+static_cast<std::size_t>(x)];
                if(y-radius-1>=0)sum-=horizontal[static_cast<std::size_t>(y-radius-1)*w+static_cast<std::size_t>(x)];
                if(y>=0) { auto& a=image.pixels[(static_cast<std::size_t>(y)*w+static_cast<std::size_t>(x))*4+3];a=static_cast<unsigned char>(a+(std::min(255U,sum/taps*shadow_strength)*(255U-a)/255U)); }
            }
        }
        shadow=false;
    }
    if(shadow) {
        const int w=image.width,h=image.height;
        std::vector<unsigned char> horizontal(static_cast<std::size_t>(w)*static_cast<std::size_t>(h));
        constexpr unsigned weights[]{1,2,2,2,1};
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            unsigned sum{};
            for(int k=-2;k<=2;++k)if(x+k>=0&&x+k<w)sum+=weights[k+2]*image.pixels[(static_cast<std::size_t>(y)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x+k))*4+3];
            horizontal[static_cast<std::size_t>(y)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x)]=static_cast<unsigned char>(sum>>3U);
        }
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            unsigned sum{};
            for(int k=-2;k<=2;++k)if(y+k>=0&&y+k<h)sum+=weights[k+2]*horizontal[static_cast<std::size_t>(y+k)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x)];
            auto& alpha=image.pixels[(static_cast<std::size_t>(y)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x))*4+3];
            alpha=static_cast<unsigned char>(alpha+(std::min(255U,(sum>>3U)*shadow_strength)*(256U-alpha)>>8U));
        }
    }
    // Convert Flash's internal premultiplied bytes to its PNG representation.
    for(std::size_t i=0;i<image.pixels.size();i+=4) {
        const unsigned alpha=image.pixels[i+3];if(!alpha || alpha==255)continue;
        for(unsigned c=0;c<3;++c)image.pixels[i+c]=static_cast<unsigned char>(std::min(255U,(static_cast<unsigned>(image.pixels[i+c])<<8U)/alpha));
    }
}
std::string_view trailing_trimmed(std::string_view line) { while(!line.empty()&&line.back()==' ')line.remove_suffix(1);return line; }
std::uint32_t character(std::string_view input,std::size_t& at) {
    const auto first=static_cast<unsigned char>(input[at++]);if(first<128)return first;
    const int extra=first>=0xc2&&first<=0xdf ? 1 : first>=0xe0&&first<=0xef ? 2 : first>=0xf0&&first<=0xf4 ? 3 : 0;
    if(!extra)return '?';
    std::uint32_t code=first&((1U<<(6-extra))-1U);
    for(int i=0;i<extra;++i) {
        if(at==input.size() || (static_cast<unsigned char>(input[at])&0xc0U)!=0x80U)return '?';
        code=(code<<6U)|(static_cast<unsigned char>(input[at++])&0x3fU);
    }
    if(code>0x10ffffU || (code>=0xd800U&&code<=0xdfffU) || (extra==1&&code<128) || (extra==2&&code<2048) || (extra==3&&code<65536))return '?';
    return code;
}
}
struct FontBook::NativeFonts {
    FT_Library library{};
    std::array<FT_Face,2> faces{};
    ~NativeFonts() { for(auto face:faces)if(face)FT_Done_Face(face);if(library)FT_Done_FreeType(library); }
};
FontBook::~FontBook()=default;
void FontBook::enable_native(const std::filesystem::path& root) {
    auto native=std::make_unique<NativeFonts>();
    if(FT_Init_FreeType(&native->library))throw std::runtime_error("Cannot initialize native font renderer");
    for(std::size_t i=0;i<2;++i) {
        const auto path=root/"src/com/company/ui/fonts"/(i ? "MyriadProBold.otf" : "MyriadPro.otf");
        if(FT_New_Face(native->library,path.string().c_str(),0,&native->faces[i]))throw std::runtime_error("Cannot load original Myriad font: "+path.string());
    }
    native_=std::move(native);
}
void FontBook::set_pixel_scale(double scale) {
    if(!std::isfinite(scale)||scale<=0||scale>16)throw std::invalid_argument("Invalid font pixel density");
    if(scale!=1&&!native_)throw std::logic_error("Native font outlines are required for resized text");
    pixel_scale_=scale;
}
TextBitmap FontBook::rasterize_at_scale(std::string_view text,int size,double scale,bool bold,bool shadow,std::uint32_t color,Vec2 pixel_phase) const {
    if(!native_||!std::isfinite(scale)||scale<=0||scale>16||
       !std::isfinite(pixel_phase.x)||!std::isfinite(pixel_phase.y)||pixel_phase.x<0||pixel_phase.x>=1||pixel_phase.y<0||pixel_phase.y>=1)
        throw std::invalid_argument("Invalid animated font density or phase");
    return rasterize_native(text,size,bold,shadow,color,0,0,1,0,0,scale,pixel_phase);
}
TextBitmap FontBook::rasterize_glow(std::string_view text,int size,bool bold,std::uint32_t color,unsigned strength) const {
    const double scale=pixel_scale_;
    auto render=[this,text=std::string(text),size,bold,color,strength,scale](Vec2 pp) {
        constexpr double padding=2;
        // The glyph origin sits padding+pp inside the bitmap: whole pixels placed by the blit, remainder by the glyphs.
        const double dx=padding*scale+pp.x,dy=padding*scale+pp.y,nx=std::floor(dx),ny=std::floor(dy);
        const auto core=rasterize_at_scale(text,size,scale,bold,false,color,{dx-nx,dy-ny});
        TextBitmap result{ui_image(core.image.width/scale+padding*2+pp.x/scale,core.image.height/scale+padding*2+pp.y/scale,scale),{-padding,-padding},core.width,core.height};
        ui_blit(result.image,core.image,nx/scale,ny/scale,false,true);
        finish(result.image,true,strength,2);return result;
    };
    auto result=render({});result.redraw=std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));return result;
}
double FontBook::line_measure(std::string_view text,int size,bool bold) const {
    if(!native_)throw std::logic_error("TextLine rendering requires original font outlines");
    auto face=native_->faces[bold ? 1 : 0];double width{};FT_UInt previous{};
    for(std::size_t at=0;at<text.size();) {
        const auto code=character(text,at);auto index=FT_Get_Char_Index(face,code);if(!index)index=FT_Get_Char_Index(face,'?');
        FT_Vector kern{};FT_Get_Kerning(face,previous,index,FT_KERNING_UNSCALED,&kern);
        if(FT_Load_Glyph(face,index,FT_LOAD_NO_SCALE))throw std::runtime_error("Cannot measure native glyph");
        width+=static_cast<double>(face->glyph->metrics.horiAdvance+kern.x)*size/face->units_per_EM;previous=index;
    }
    return width;
}
double FontBook::line_ascent(int size,bool bold) const {
    if(!native_)throw std::logic_error("TextLine rendering requires original font outlines");
    const auto face=native_->faces[bold ? 1 : 0];return static_cast<double>(face->ascender)*size/face->units_per_EM;
}
TextBitmap FontBook::rasterize_line(std::string_view text,double scale,std::uint32_t color,int size,bool bold) const {
    if(!native_||!std::isfinite(scale)||scale<=0||scale>16)throw std::invalid_argument("Invalid chat text density");
    auto bitmap=rasterize_native(text,size,bold,false,color,0,0,1,0,0,scale,{},true);
    bitmap.offset.y=-line_ascent(size,bold);return bitmap;
}
TextBitmap FontBook::rasterize_native(std::string_view text,int size,bool bold,bool shadow,std::uint32_t color,double phase,double vertical_phase,unsigned strength,int field_width,int field_height,double density,Vec2 pixel_phase,bool natural_metrics) const {
    REALM_ZONE("text.rasterize");
    auto run=layout(text,size,bold,false,phase,vertical_phase);
    if(natural_metrics) { run.width=line_measure(text,size,bold)+4;run.height=size+4; }
    if(field_width<0||field_height<0)throw std::invalid_argument("Invalid text field size");
    const double padding=shadow ? 8 : 0,s=density;
    const double w=field_width ? field_width : run.width,h=field_height ? field_height : run.height;
    TextBitmap result{ui_image(std::max(1.0,w+padding*2+pixel_phase.x/s),std::max(1.0,h+padding*2+pixel_phase.y/s),s),{-padding-phase,-padding},run.width,run.height};
    auto face=native_->faces[bold ? 1 : 0];
    // Place the outlines as the original TextField does: after its 2 px gutter, with the first baseline at the
    // gutter plus the embedded font's ascent (usWinAscent; the baked line heights are win ascent + descent).
    // Keep the logical advances/kerning so larger rendering does not change wrapping or hit areas.
    const auto& logical=faces_.at(style(size,bold,false,vertical_phase));
    const auto* os2=static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face,FT_SFNT_OS2));
    if(!natural_metrics&&!os2)throw std::runtime_error("Cannot read native font metrics");
    const double gutter=natural_metrics ? 0 : 2;
    const double baseline=natural_metrics ? line_ascent(size,bold) : gutter+static_cast<double>(os2->usWinAscent)*size/face->units_per_EM;
    if(FT_Set_Char_Size(face,0,static_cast<FT_F26Dot6>(std::lround(size*s*64)),72,72))throw std::runtime_error("Cannot size native font");
    double x{},y{};std::uint32_t previous{};
    for(std::size_t at=0;at<text.size();) {
        auto code=character(text,at);if(code=='\r')continue;
        if(code=='\n') { x=0;y+=logical.line_advance;previous=0;continue; }
        auto glyph=logical.glyphs.find(code*32U);if(glyph==logical.glyphs.end()||!FT_Get_Char_Index(face,code)) { code='?';glyph=logical.glyphs.find(code*32U); }
        if(glyph==logical.glyphs.end())continue;
        double natural_advance{};
        if(natural_metrics) {
            const auto index=FT_Get_Char_Index(face,code);FT_Vector kern{};
            FT_Get_Kerning(face,FT_Get_Char_Index(face,previous),index,FT_KERNING_UNSCALED,&kern);
            if(FT_Load_Glyph(face,index,FT_LOAD_NO_SCALE))throw std::runtime_error("Cannot measure chat glyph");
            x+=static_cast<double>(kern.x)*size/face->units_per_EM;
            natural_advance=static_cast<double>(face->glyph->metrics.horiAdvance)*size/face->units_per_EM;
        } else if(const auto kern=logical.kerning.find((static_cast<std::uint64_t>(previous)<<32U)|code);kern!=logical.kerning.end())x+=kern->second;
        const double origin=(x+phase+padding+gutter)*s+pixel_phase.x,by=(y+baseline+vertical_phase+padding)*s+pixel_phase.y;
        FT_Vector delta{static_cast<FT_Pos>(std::lround((origin-std::floor(origin))*64)),static_cast<FT_Pos>(-std::lround((by-std::floor(by))*64))};
        FT_Set_Transform(face,nullptr,&delta);
        if(FT_Load_Char(face,code,FT_LOAD_RENDER|FT_LOAD_NO_HINTING))throw std::runtime_error("Cannot rasterize native glyph");
        const auto& bitmap=face->glyph->bitmap;
        const int ox=static_cast<int>(std::floor(origin))+face->glyph->bitmap_left,oy=static_cast<int>(std::floor(by))-face->glyph->bitmap_top;
        for(unsigned row=0;row<bitmap.rows;++row)for(unsigned col=0;col<bitmap.width;++col) {
            const int px=ox+static_cast<int>(col),py=oy+static_cast<int>(row);
            if(px<padding*s+pixel_phase.x||py<padding*s+pixel_phase.y||px>=(padding+w-2+phase)*s+pixel_phase.x||py>=(padding+h-2+vertical_phase)*s+pixel_phase.y||px<0||py<0||px>=result.image.width||py>=result.image.height)continue;
            const auto dst=(static_cast<std::size_t>(py)*static_cast<std::size_t>(result.image.width)+static_cast<std::size_t>(px))*4;
            const auto a=bitmap.buffer[static_cast<std::ptrdiff_t>(row)*bitmap.pitch+col];
            const unsigned combined=std::min(255U,static_cast<unsigned>(result.image.pixels[dst+3])+a);
            result.image.pixels[dst+3]=static_cast<unsigned char>(combined);
            for(unsigned c=0;c<3;++c)result.image.pixels[dst+c]=static_cast<unsigned char>(((color>>(16-c*8))&255U)*(combined+1U)>>8U);
        }
        x+=natural_metrics ? natural_advance : glyph->second.advance;previous=code;
    }
    FT_Set_Transform(face,nullptr,nullptr);finish(result.image,shadow,strength);return result;
}
FontBook::FontBook(const std::filesystem::path& directory):directory_(directory) {
    if(directory.empty())return;
    const bool indexed=std::filesystem::exists(directory/"fontpack.json");
    std::ifstream input(directory/(indexed ? "fontpack.json" : "manifest.json"));if(!input)throw std::runtime_error("Missing baked font manifest");
    const auto manifest=nlohmann::json::parse(input);
    if(manifest.at("schema")!=(indexed ? 2 : 1) || manifest.at("source_sha256")!=flash::source_sha256)throw std::runtime_error("Baked fonts do not match current Flash source");
    if(indexed&&(!manifest.contains("bundle")||manifest.value("bundle_format",std::string{})!="alpha-zlib"))throw std::runtime_error("Unsupported indexed font bundle");
    std::uint64_t bundle_size{};
    auto valid_path=[](const std::string& path) { return !path.empty()&&!std::filesystem::path(path).is_absolute()&&path.find("..") == std::string::npos; };
    if(manifest.contains("bundle")) {
        const auto path=manifest.at("bundle").get<std::string>();if(!valid_path(path))throw std::runtime_error("Invalid font bundle path");
        bundle_.open(directory/path,std::ios::binary);if(!bundle_)throw std::runtime_error("Missing font glyph bundle");
        bundle_size=std::filesystem::file_size(directory/path);
    }
    bundle_size_=bundle_size;
    std::uint64_t index_records{};
    if(indexed) {
        const auto path=manifest.at("index").get<std::string>();if(!valid_path(path)||manifest.at("record_bytes")!=32)throw std::runtime_error("Invalid font index");
        index_.open(directory/path,std::ios::binary);if(!index_)throw std::runtime_error("Missing font index");
        const auto bytes=std::filesystem::file_size(directory/path);if(bytes%32||bytes>64*1024*1024)throw std::runtime_error("Invalid font index size");index_records=bytes/32;
    }
    for(const auto& font:manifest.at("fonts")) {
        if(font.at("shadow").get<bool>())continue;
        FontFace face;face.line_height=font.at("line_height").get<double>();face.line_advance=font.value("line_advance",face.line_height-4);face.phases=font.value("phases",1);
        if(face.phases!=1&&face.phases!=20)throw std::runtime_error("Invalid font phase count");
        if(!std::isfinite(face.line_height)||!std::isfinite(face.line_advance)||face.line_height<1||face.line_height>128||face.line_advance<1||face.line_advance>128)throw std::runtime_error("Invalid font line metrics");
        if(indexed) {
            const auto first=font.at("first").get<std::uint64_t>(),count=font.at("count").get<std::uint64_t>();
            if(first>index_records||count>index_records-first)throw std::runtime_error("Invalid font index range");
            face.first=first;face.count=count;face.loaded=false;
        } else {
        for(const auto& entry:font.at("glyphs")) {
            GlyphSource source;
            if(bundle_.is_open()) {
                source.offset=entry.at("offset").get<std::uint64_t>();source.length=entry.at("bytes").get<std::uint32_t>();
                if(source.length>1024*1024||source.offset>bundle_size||source.length>bundle_size-source.offset)throw std::runtime_error("Invalid bundled glyph range");
                if(manifest.value("bundle_format",std::string{})=="alpha-zlib") {
                    source.width=entry.at("width").get<int>();source.height=entry.at("height").get<int>();
                    if(source.width<1||source.height<1||source.width>512||source.height>512)throw std::runtime_error("Invalid bundled glyph dimensions");
                }
            } else { const auto path=entry.at("file").get<std::string>();if(!valid_path(path))throw std::runtime_error("Invalid font glyph path");source.path=static_cast<std::uint32_t>(paths_.size());paths_.push_back(path); }
            Glyph glyph{sources_.size(),{entry.at("x").get<double>(),entry.at("y").get<double>()},entry.at("advance").get<double>(),entry.at("empty").get<bool>()};
            if(!face.glyphs.emplace(entry.at("code").get<std::uint32_t>()*32U+entry.value("phase",0U),glyph).second)throw std::runtime_error("Duplicate font glyph");
            sources_.push_back(std::move(source));
        }
        }
        for(const auto& pair:font.at("kerning"))face.kerning.emplace((pair[0].get<std::uint64_t>()<<32U)|pair[1].get<std::uint32_t>(),pair[2].get<double>());
        if(!faces_.emplace(style(font.at("size").get<int>(),font.at("bold").get<bool>(),font.at("shadow").get<bool>(),font.value("vertical_phase",0.0)),std::move(face)).second)throw std::runtime_error("Duplicate baked font style");
    }
}
void FontBook::load_face(FontFace& face) const {
    if(face.loaded)return;
    index_.clear();index_.seekg(static_cast<std::streamoff>(face.first*32));face.glyphs.reserve(static_cast<std::size_t>(face.count));
    for(std::uint64_t record=0;record<face.count;++record) {
        unsigned char bytes[32];if(!index_.read(reinterpret_cast<char*>(bytes),32))throw std::runtime_error("Cannot read font index");
        auto u16=[&](unsigned at){return static_cast<std::uint16_t>(bytes[at]|(static_cast<unsigned>(bytes[at+1])<<8U));};
        auto u32=[&](unsigned at){return static_cast<std::uint32_t>(u16(at))|(static_cast<std::uint32_t>(u16(at+2))<<16U);};
        const auto code=u32(0);const auto phase=bytes[4];
        const double advance=std::bit_cast<double>(static_cast<std::uint64_t>(u32(14))|(static_cast<std::uint64_t>(u32(18))<<32U));
        GlyphSource source{u32(22),u32(26),u16(10),u16(12),0};
        if(code>=65536||(phase>=face.phases&&phase!=20&&phase!=21)||!std::isfinite(advance)||advance<0||advance>1024||source.width<1||source.height<1||source.width>512||source.height>512||source.length>1024*1024||source.offset>bundle_size_||source.length>bundle_size_-source.offset)throw std::runtime_error("Invalid indexed glyph");
        Glyph glyph{sources_.size(),{static_cast<double>(std::bit_cast<std::int16_t>(u16(6))),static_cast<double>(std::bit_cast<std::int16_t>(u16(8)))},advance,bytes[5]!=0};
        if(!face.glyphs.emplace(code*32U+phase,glyph).second)throw std::runtime_error("Duplicate indexed glyph");
        sources_.push_back(std::move(source));
    }
    face.loaded=true;
}
const Image& FontBook::image(std::size_t index) const {
    if(auto found=cache_.find(index);found!=cache_.end()) { telemetry::add(telemetry::Counter::glyph_hits); lru_.splice(lru_.end(),lru_,found->second.position);return found->second.image; }
    telemetry::add(telemetry::Counter::glyph_misses);
    const auto& source=sources_.at(index);Image decoded;
    if(bundle_.is_open()) {
        std::vector<unsigned char> bytes(static_cast<std::size_t>(source.length));bundle_.clear();bundle_.seekg(static_cast<std::streamoff>(source.offset));
        if(!bundle_.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Cannot read bundled font glyph");
        if(source.width) {
            std::vector<unsigned char> alpha(static_cast<std::size_t>(source.width)*static_cast<std::size_t>(source.height));uLongf length=alpha.size();
            if(uncompress(alpha.data(),&length,bytes.data(),bytes.size())!=Z_OK||length!=alpha.size())throw std::runtime_error("Invalid compressed glyph alpha data");
            decoded={source.width,source.height,std::vector<unsigned char>(alpha.size()*4)};
            for(std::size_t i=0;i<alpha.size();++i)for(unsigned c=0;c<4;++c)decoded.pixels[i*4+c]=alpha[i];
        } else { decoded=load_png(bytes);premultiply_flash_bitmap(decoded); }
    } else { decoded=load_png(directory_/paths_.at(source.path));premultiply_flash_bitmap(decoded); }
    constexpr std::size_t limit=8*1024*1024;
    while(!cache_.empty()&&(cache_bytes_+decoded.pixels.size()>limit||cache_.size()>=4096)) {
        const auto oldest=cache_.find(lru_.front());
        cache_bytes_-=oldest->second.image.pixels.size();cache_.erase(oldest);lru_.pop_front();++cache_evictions_;telemetry::add(telemetry::Counter::glyph_evictions);
    }
    cache_bytes_+=decoded.pixels.size();lru_.push_back(index);
    return cache_.emplace(index,CachedGlyph{std::move(decoded),std::prev(lru_.end())}).first->second.image;
}
TextRun FontBook::layout(std::string_view text,int size,bool bold,bool,double phase,double vertical_phase) const {
    if(!std::isfinite(phase)||phase<0||phase>=1)throw std::runtime_error("Invalid text horizontal phase");
    const auto found=faces_.find(style(size,bold,false,vertical_phase));if(found==faces_.end())throw std::runtime_error("Missing baked font style; regenerate the font pack with tools/bake_fonts.py");
    load_face(found->second);
    const auto& face=found->second;TextRun run;run.glyphs.reserve(text.size());double x{},y{};std::uint32_t previous{};std::size_t lines=1;
    for(std::size_t at=0;at<text.size();) {
        auto code=character(text,at);
        if(code=='\r')continue;
        if(code=='\n') { run.width=std::max(run.width,x);x=0;y+=face.line_advance;++lines;previous=0;continue; }
        auto glyph=face.glyphs.find(code*32U);if(glyph==face.glyphs.end()) { code='?';glyph=face.glyphs.find(code*32U); }
        if(glyph==face.glyphs.end())continue;
        const auto pair=face.kerning.find((static_cast<std::uint64_t>(previous)<<32U)|code);if(pair!=face.kerning.end())x+=pair->second;
        const double snapped=std::round((x+phase)*20)/20;
        if(face.phases>1) { const auto slot=static_cast<std::uint32_t>(std::llround((snapped-std::floor(snapped))*20))%20U;glyph=face.glyphs.find(code*32U+slot); }
        if(snapped==0) { const auto initial=face.glyphs.find(code*32U+20);if(initial!=face.glyphs.end())glyph=initial; }
        if(snapped==.5) { const auto initial=face.glyphs.find(code*32U+21);if(initial!=face.glyphs.end())glyph=initial; }
        if(glyph==face.glyphs.end())throw std::runtime_error("Missing baked glyph phase");
        if(!glyph->second.empty)run.glyphs.push_back({glyph->second.image,{std::floor(snapped)+glyph->second.offset.x,y+glyph->second.offset.y}});
        x+=glyph->second.advance;previous=code;
    }
    run.width=std::floor(std::max(run.width,x)+4);run.height=static_cast<double>(lines)*face.line_height;return run;
}
double FontBook::line_advance(int size,bool bold) const {
    const auto found=faces_.find(style(size,bold,false));
    if(found==faces_.end())throw std::runtime_error("Missing baked font style");
    return found->second.line_advance;
}
double FontBook::measure(std::string_view text,int size,bool bold) const {
    const auto found=faces_.find(style(size,bold,false));if(found==faces_.end())throw std::runtime_error("Missing baked font style; regenerate the font pack with tools/bake_fonts.py");
    load_face(found->second);const auto& face=found->second;double x{};std::uint32_t previous{};
    for(std::size_t at=0;at<text.size();) {
        auto code=character(text,at);auto glyph=face.glyphs.find(code*32U);if(glyph==face.glyphs.end()) { code='?';glyph=face.glyphs.find(code*32U); }
        if(glyph==face.glyphs.end())continue;
        const auto pair=face.kerning.find((static_cast<std::uint64_t>(previous)<<32U)|code);if(pair!=face.kerning.end())x+=pair->second;
        x+=glyph->second.advance;previous=code;
    }
    return x;
}
TextBitmap FontBook::rasterize(std::string_view text,int size,bool bold,bool shadow,std::uint32_t color,double phase,double vertical_phase,unsigned shadow_strength,int field_width,int field_height) const {
    if(native_&&pixel_scale_!=1) {
        const double scale=pixel_scale_;
        auto render=[this,text=std::string(text),size,bold,shadow,color,phase,vertical_phase,shadow_strength,field_width,field_height,scale](Vec2 pp) {
            return rasterize_native(text,size,bold,shadow,color,phase,vertical_phase,shadow_strength,field_width,field_height,scale,pp);
        };
        auto result=render({});result.redraw=std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));return result;
    }
    const auto run=layout(text,size,bold,false,phase,vertical_phase);
    const int padding=shadow ? 8 : 0;
    if(run.width>8192 || run.height>8192)throw std::runtime_error("Text field exceeds size limit");
    if(field_width<0||field_width>8192||field_height<0||field_height>8192)throw std::runtime_error("Invalid text field size");
    const int width=field_width ? field_width : static_cast<int>(run.width),height=field_height ? field_height : static_cast<int>(run.height);
    TextBitmap result{{std::max(1,width+2*padding),std::max(1,height+2*padding),{}},{-static_cast<double>(padding)-phase,-static_cast<double>(padding)},run.width,run.height};
    result.image.pixels.resize(static_cast<std::size_t>(result.image.width)*static_cast<std::size_t>(result.image.height)*4);
    for(const auto& glyph:run.glyphs)composite(result.image,image(glyph.image),static_cast<int>(glyph.position.x),static_cast<int>(glyph.position.y),padding,width-2+phase,height-2+vertical_phase,color);
    finish(result.image,shadow,shadow_strength);
    return result;
}
TextBitmap FontBook::rasterize_spans(std::span<const TextSpan> spans,int size,bool shadow,std::uint32_t color,double bold_vertical_phase) const {
    if(native_&&pixel_scale_!=1) {
        struct Owned { std::string text;bool bold{}; };
        std::vector<Owned> owned;owned.reserve(spans.size());
        for(const auto& span:spans)owned.push_back({std::string(span.text),span.bold});
        const double scale=pixel_scale_;
        auto render=[this,owned=std::move(owned),size,shadow,color,bold_vertical_phase,scale](Vec2 pp) {
            double advance{},height{};
            for(const auto& span:owned) { advance+=measure(span.text,size,span.bold);height=std::max(height,layout(span.text,size,span.bold).height); }
            const double padding=shadow ? 8 : 0;
            TextBitmap result{ui_image(std::floor(advance+4)+2*padding+pp.x/scale,height+2*padding+pp.y/scale,scale),{-padding,-padding},std::floor(advance+4),height};
            advance=0;
            for(const auto& span:owned) {
                if(span.text.empty())continue;
                const double dx=(padding+advance)*scale+pp.x,dy=padding*scale+pp.y,nx=std::floor(dx),ny=std::floor(dy);
                const auto part=rasterize_native(span.text,size,span.bold,false,color,0,span.bold ? bold_vertical_phase : 0,1,0,0,scale,{dx-nx,dy-ny});
                ui_blit(result.image,part.image,nx/scale,ny/scale);
                advance+=measure(span.text,size,span.bold);
            }
            // Apply the filter once to the complete field, including mixed spans.
            premultiply_flash_bitmap(result.image);
            finish(result.image,shadow,1);
            return result;
        };
        auto result=render({});result.redraw=std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));return result;
    }
    TextRun combined;double advance{};
    for(const auto& span:spans) {
        if(span.text.empty())continue;
        const auto run=layout(span.text,size,span.bold,false,advance-std::floor(advance),span.bold ? bold_vertical_phase : 0);
        for(const auto& glyph:run.glyphs)combined.glyphs.push_back({glyph.image,{glyph.position.x+std::floor(advance),glyph.position.y}});
        advance+=measure(span.text,size,span.bold);combined.height=std::max(combined.height,run.height);
    }
    combined.width=std::floor(advance+4);const int padding=shadow ? 8 : 0;
    if(combined.width>8192||combined.height>8192)throw std::runtime_error("Text field exceeds size limit");
    const int w=static_cast<int>(combined.width),h=static_cast<int>(combined.height);
    TextBitmap result{{std::max(1,w+padding*2),std::max(1,h+padding*2),{}},{-static_cast<double>(padding),-static_cast<double>(padding)},combined.width,combined.height};
    result.image.pixels.resize(static_cast<std::size_t>(result.image.width)*static_cast<std::size_t>(result.image.height)*4);
    for(const auto& glyph:combined.glyphs)composite(result.image,image(glyph.image),static_cast<int>(glyph.position.x),static_cast<int>(glyph.position.y),padding,w-2,h-2,color);
    finish(result.image,shadow,1);return result;
}
std::vector<std::string> FontBook::wrap(std::string_view text,int size,bool bold,int field_width) const {
    const double limit=field_width-4;std::vector<std::string> lines;std::string current;
    for(std::size_t at=0;at<=text.size();) {
        const auto newline=text.find('\n',at);const auto paragraph=text.substr(at,newline==std::string_view::npos ? std::string_view::npos : newline-at);
        for(std::size_t begin=0;begin<paragraph.size();) {
            // A token is a word with the spaces after it; those spaces do not
            // count toward the line width.
            auto end=paragraph.find_first_not_of(' ',begin);end=end==std::string_view::npos ? paragraph.size() : paragraph.find(' ',end);
            end=end==std::string_view::npos ? paragraph.size() : paragraph.find_first_not_of(' ',end);if(end==std::string_view::npos)end=paragraph.size();
            const auto token=paragraph.substr(begin,end-begin);begin=end;
            const auto word=trailing_trimmed(token);
            if(!current.empty()&&measure(trailing_trimmed(current+std::string(word)),size,bold)>limit) { lines.push_back(std::move(current));current.clear(); }
            if(current.empty()&&measure(word,size,bold)>limit) {
                // A word wider than the field breaks between characters.
                for(std::size_t c=0;c<token.size();) {
                    const auto from=c;(void)character(token,c);const auto piece=token.substr(from,c-from);
                    if(!current.empty()&&piece!=" "&&measure(trailing_trimmed(current+std::string(piece)),size,bold)>limit) { lines.push_back(std::move(current));current.clear(); }
                    current+=piece;
                }
                continue;
            }
            current+=token;
        }
        lines.push_back(std::move(current));current.clear();
        if(newline==std::string_view::npos)break;
        at=newline+1;
    }
    return lines;
}
TextBitmap FontBook::rasterize_centered(std::string_view text,int size,bool bold,bool shadow,std::uint32_t color,int field_width,unsigned shadow_strength) const {
    if(field_width<4||field_width>8192)throw std::runtime_error("Invalid text field width");
    const auto found=faces_.find(style(size,bold,false));if(found==faces_.end())throw std::runtime_error("Missing baked font style; regenerate the font pack with tools/bake_fonts.py");
    const auto lines=wrap(text,size,bold,field_width);
    // Line positions are twips: x = 2 + (width - 4 - line width) / 2, floored.
    const auto advance=std::lround(found->second.line_advance*20);
    const double height=static_cast<double>(lines.size())*static_cast<double>(advance)/20+4;
    // Long 14pt regular fields can require vertical phases absent from the
    // original baked pack. Use the native outlines instead of requiring a
    // larger phase pack solely for glyph-edge parity.
    bool missing_phase=false;
    for(std::size_t i=0;i<lines.size();++i) {
        const auto twips=static_cast<long>(i)*advance+(i ? 4 : 0);
        const double y=static_cast<double>(twips/5*5)/20;
        missing_phase|=!faces_.contains(style(size,bold,false,y-std::floor(y)));
    }
    if(native_&&(pixel_scale_!=1||missing_phase)) {
        const double scale=pixel_scale_;
        auto render=[this,lines,size,bold,shadow,color,field_width,shadow_strength,advance,height,scale](Vec2 pp) {
            const double padding=shadow ? 8 : 0;
            TextBitmap result{ui_image(field_width+2*padding+pp.x/scale,std::ceil(height)+2*padding+pp.y/scale,scale),{-padding,-padding},static_cast<double>(field_width),height};
            for(std::size_t i=0;i<lines.size();++i) {
                const double dx=(padding+(field_width-4-measure(trailing_trimmed(lines[i]),size,bold))/2)*scale+pp.x;
                const double dy=(padding+static_cast<double>(i)*static_cast<double>(advance)/20)*scale+pp.y,nx=std::floor(dx),ny=std::floor(dy);
                const auto part=rasterize_native(lines[i],size,bold,false,color,0,0,1,0,0,scale,{dx-nx,dy-ny});
                ui_blit(result.image,part.image,nx/scale,ny/scale);
            }
            premultiply_flash_bitmap(result.image);finish(result.image,shadow,shadow_strength);return result;
        };
        auto result=render({});
        if(scale!=1)result.redraw=std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));
        return result;
    }
    if(height>8192)throw std::runtime_error("Text field exceeds size limit");
    const int padding=shadow ? 8 : 0;
    TextBitmap result{{field_width+2*padding,static_cast<int>(std::ceil(height))+2*padding,{}},{-static_cast<double>(padding),-static_cast<double>(padding)},static_cast<double>(field_width),height};
    result.image.pixels.resize(static_cast<std::size_t>(result.image.width)*static_cast<std::size_t>(result.image.height)*4);
    for(std::size_t i=0;i<lines.size();++i) {
        const auto width=std::lround(measure(trailing_trimmed(lines[i]),size,bold)*20);
        const auto twips=static_cast<long>(std::floor(static_cast<double>((field_width-4)*20-width)/2));
        // Lines after the first sit 4 twips below their TextLineMetrics position,
        // floored to Flash's quarter-pixel vertical glyph grid (measured on
        // second to fifth 16- and 18-pixel bold lines).
        const auto line_twips=static_cast<long>(i)*advance+(i ? 4 : 0);
        const double x=static_cast<double>(twips)/20,y=static_cast<double>(line_twips/5*5)/20;
        const auto run=layout(lines[i],size,bold,false,x-std::floor(x),y-std::floor(y));
        for(const auto& glyph:run.glyphs)composite(result.image,image(glyph.image),static_cast<int>(std::floor(x)+glyph.position.x),static_cast<int>(std::floor(y)+glyph.position.y),padding,field_width-2,height-2,color);
    }
    finish(result.image,shadow,shadow_strength);
    return result;
}
TextBitmap redrawable_text(std::function<TextBitmap(Vec2)> render) {
    auto result=render({});result.redraw=std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));return result;
}
TextBitmap place_text(const TextBitmap& text,double scale,double& x,double& y) {
    if(!text.redraw||scale==1||text.image.pixel_scale!=scale||!std::isfinite(x)||!std::isfinite(y))return text;
    const double dx=x*scale,dy=y*scale,nx=std::floor(dx),ny=std::floor(dy);
    auto exact=(*text.redraw)({dx-nx,dy-ny});
    x=nx/scale;y=ny/scale;return exact;
}
void ui_blit_text(Image& destination,const TextBitmap& text,double x,double y,bool source_premultiplied,bool destination_premultiplied,double opacity,
                  double clip_x,double clip_y,double clip_width,double clip_height) {
    const auto placed=place_text(text,destination.pixel_scale,x,y);
    ui_blit(destination,placed.image,x,y,source_premultiplied,destination_premultiplied,opacity,clip_x,clip_y,clip_width,clip_height);
}
}
