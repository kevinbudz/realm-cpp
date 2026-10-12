#include "realm/telemetry.hpp"
#include "realm/appearance.hpp"
#include "flash_assets.hpp"
#include <array>
#include <algorithm>
#include <cmath>
namespace realm {
namespace {
// std::lround for a non-negative value without the libm call (truncate, then round the fraction half up; exact).
inline long round_up(double v) noexcept { const auto r=static_cast<long>(v);return v-static_cast<double>(r)>=.5 ? r+1 : r; }
}
std::array<std::uint32_t,2> world_dyes(const World& world,const Entity& entity) {
    std::array<std::uint32_t,2> result{entity.tex1,entity.tex2};
    if(!entity.player||entity.id!=world.player_id())return result;
    REALM_ZONE("scene.world_dyes");
    const int x=static_cast<int>(entity.position.x),y=static_cast<int>(entity.position.y);
    const int dx=entity.position.x-x>.5 ? 1 : -1,dy=entity.position.y-y>.5 ? 1 : -1;
    // The first Merchant in (neighbour order {0,0},{1,0},{0,1},{1,1}, then entity order) decides the dye. One pass over the
    // merchants (in entity order) finds it: the four neighbour tiles are distinct, so each belongs to at most one neighbour slot.
    const Entity* merchant{};int slot=4;
    world.each_merchant([&](const Entity& e) {
        if(!slot)return;
        const int ox=static_cast<int>(e.position.x)-x,oy=static_cast<int>(e.position.y)-y;
        const int kx=ox==0 ? 0 : ox==dx ? 1 : -1,ky=oy==0 ? 0 : oy==dy ? 1 : -1;
        if(kx<0||ky<0||kx+2*ky>=slot)return;
        const auto* d=world.catalog().object(e.type);
        if(!d||d->class_name!="Merchant")return;
        merchant=&e;slot=kx+2*ky;
    });
    if(!merchant)return result;
    const auto& e=*merchant;
    const double ex=e.position.x-entity.position.x,ey=e.position.y-entity.position.y;
    if(ex*ex+ey*ey>=1)return result;
    const auto it=e.stats.find(static_cast<std::uint8_t>(flash::StatId::MERCHANDISE_TYPE_STAT));
    const auto* item=it==e.stats.end() ? nullptr : world.catalog().object(static_cast<std::uint16_t>(it->second));
    if(item&&item->xml.child("Activate").text()=="Dye") {
        if(item->xml.has("Tex1"))result[0]=static_cast<std::uint32_t>(item->xml.number("Tex1"));
        if(item->xml.has("Tex2"))result[1]=static_cast<std::uint32_t>(item->xml.number("Tex2"));
    }
    return result;
}
Appearances::Appearances(const std::filesystem::path& root,const AnimationLibrary& animations):animations_(animations) {
    textile_sheets_.fill(-1);
    for(std::size_t sheet=0;sheet<flash::sheets.size();++sheet) {
        const std::string_view name=flash::sheets[sheet].name;
        if(name=="textile4x4")textile_sheets_[0]=static_cast<int>(sheet);else if(name=="textile5x5")textile_sheets_[1]=static_cast<int>(sheet);
        else if(name=="textile9x9")textile_sheets_[2]=static_cast<int>(sheet);else if(name=="textile10x10")textile_sheets_[3]=static_cast<int>(sheet);
    }
    sheets_.resize(flash::assets.size());
    // Only image-set assets are needed. Animation strips already have owners.
    for(const auto& sheet:flash::sheets)if(sheet.asset>=0&&sheets_[static_cast<std::size_t>(sheet.asset)].pixels.empty()) {
        auto& image=sheets_[static_cast<std::size_t>(sheet.asset)];image=load_png(root/flash::assets[static_cast<std::size_t>(sheet.asset)].path);normalize_flash_png(image);
    }
}
Image Appearances::sprite(int sheet,int index) const {
    if(sheet<0||static_cast<std::size_t>(sheet)>=flash::sheets.size())return {1,1,std::vector<unsigned char>(4)};
    const auto& s=flash::sheets[static_cast<std::size_t>(sheet)];
    if(s.asset<0||index<0||index>=s.count)return {1,1,std::vector<unsigned char>(4)};
    const auto& image=sheets_[static_cast<std::size_t>(s.asset)];const int columns=image.width/s.width;
    return crop(image,index%columns*s.width,index/columns*s.height,s.width,s.height);
}
std::shared_ptr<const Image> Appearances::condition_icon(int frame) const {
    const auto sheet=std::find_if(flash::sheets.begin(),flash::sheets.end(),[](const auto& value){return value.name=="lofiInterface2";});
    if(sheet==flash::sheets.end())return {};
    const auto source=this->sprite(static_cast<int>(sheet-flash::sheets.begin()),frame);
    constexpr int size=16,margin=4;
    Image icon{size,size,std::vector<unsigned char>(size*size*4)};
    for(int y=0;y<source.height&&y+margin<size;++y)for(int x=0;x<source.width&&x+margin<size;++x)
        for(int k=0;k<4;++k)icon.pixels[static_cast<std::size_t>(((y+margin)*size+x+margin)*4+k)]=source.pixels[static_cast<std::size_t>((y*source.width+x)*4+k)];
    // The SUBTRACT gradient runs from half height down, up to 0x28 at the bottom edge.
    for(int y=0;y<size;++y) {
        const int subtract=static_cast<int>(40*std::clamp((2.0*(y+.5)/size-1),0.0,1.0));
        for(int x=0;x<size;++x) {
            const auto at=static_cast<std::size_t>((y*size+x)*4);
            for(int k=0;k<3;++k)icon.pixels[at+static_cast<std::size_t>(k)]=static_cast<unsigned char>(std::max(0,icon.pixels[at+static_cast<std::size_t>(k)]-subtract));
        }
    }
    // OUTLINE_FILTER (black, alpha 0.8, blur 1.4, strength 255): every pixel next to the art.
    // GLOW_FILTER (black, alpha 0.3, blur 6, strength 2): a six-wide box blur of the outlined alpha.
    std::vector<double> outlined(static_cast<std::size_t>(size*size));
    for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
        int near=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int sx=x+dx,sy=y+dy;if(sx<0||sy<0||sx>=size||sy>=size)continue;
            near=std::max(near,static_cast<int>(icon.pixels[static_cast<std::size_t>((sy*size+sx)*4+3)]));
        }
        const double a=icon.pixels[static_cast<std::size_t>((y*size+x)*4+3)]/255.0;
        outlined[static_cast<std::size_t>(y*size+x)]=a+near*.8/255.0*(1-a);
    }
    std::vector<double> halo(outlined.size());
    for(int pass=0;pass<2;++pass)for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
        double sum=0;
        for(int t=-3;t<3;++t) {
            const int sx=pass ? x : x+t,sy=pass ? y+t : y;
            if(sx>=0&&sy>=0&&sx<size&&sy<size)sum+=(pass ? halo : outlined)[static_cast<std::size_t>(sy*size+sx)];
        }
        (pass ? outlined : halo)[static_cast<std::size_t>(y*size+x)]=sum/6;
    }
    auto result=std::make_shared<Image>(icon);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
        const auto at=static_cast<std::size_t>((y*size+x)*4);
        const double art_alpha=icon.pixels[at+3]/255.0;
        double near=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int sx=x+dx,sy=y+dy;if(sx<0||sy<0||sx>=size||sy>=size)continue;
            near=std::max(near,icon.pixels[static_cast<std::size_t>((sy*size+sx)*4+3)]/255.0);
        }
        // Black behind the art: the outline, then the glow behind both.
        const double outline=near*.8*(1-art_alpha),glow=std::min(1.0,outlined[static_cast<std::size_t>(y*size+x)]*2)*.3;
        const double behind=outline+glow*(1-outline-art_alpha);
        const double out=art_alpha+std::max(0.0,behind)*(1-art_alpha);
        result->pixels[at+3]=static_cast<unsigned char>(std::clamp(std::lround(out*255),0L,255L));
        if(out>0)for(int k=0;k<3;++k)result->pixels[at+static_cast<std::size_t>(k)]=static_cast<unsigned char>(std::lround(icon.pixels[at+static_cast<std::size_t>(k)]*art_alpha/out));
    }
    return result;
}
const Image& Appearances::textile(std::uint32_t op) const {
    static const Image none;static const Image blank{1,1,std::vector<unsigned char>(4)};
    const auto kind=op>>24U;
    if(kind!=4&&kind!=5&&kind!=9&&kind!=10)return none;
    // The last sheet carrying the name wins, as before; the (kind, index) art is cropped once and kept (bounded by the sheet's count).
    const int sheet=textile_sheets_[kind==4 ? 0 : kind==5 ? 1 : kind==9 ? 2 : 3];
    if(sheet<0)return none;
    const int index=static_cast<int>(op&0xffffffU);const auto& s=flash::sheets[static_cast<std::size_t>(sheet)];
    if(s.asset<0||index<0||index>=s.count)return blank;
    const auto key=(static_cast<std::uint64_t>(sheet)<<32U)|static_cast<std::uint32_t>(index);
    auto found=textiles_.find(key);
    if(found==textiles_.end())found=textiles_.emplace(key,sprite(sheet,index)).first;
    return found->second;
}
Image Appearances::compose(const Image& source,const Image* mask,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const {
    REALM_ZONE("appearance.compose");
    // Flash does texturing on a 5x bitmap before size scaling. Textile origin is
    // the frame origin, including transparent extended-attack padding.
    const bool dyed=mask&&(tex1||tex2);
    Image dyed_source;
    const Image* input=&source;
    if(dyed) {
        dyed_source={source.width*5,source.height*5,std::vector<unsigned char>(static_cast<std::size_t>(source.width*5)*static_cast<std::size_t>(source.height*5)*4)};
        const Image* textiles[2]={};const std::uint32_t ops[2]={tex1,tex2};
        for(int channel=0;channel<2;++channel) textiles[channel]=&textile(ops[channel]);
        for(int y=0;y<dyed_source.height;++y)for(int x=0;x<dyed_source.width;++x) {
            const auto src=static_cast<std::size_t>((y/5*source.width+x/5)*4),dst=static_cast<std::size_t>((y*dyed_source.width+x)*4);
            std::copy_n(source.pixels.data()+src,4,dyed_source.pixels.data()+dst);
            if(x/5>=mask->width||y/5>=mask->height)continue;
            const auto m=static_cast<std::size_t>((y/5*mask->width+x/5)*4);
            if(!mask->pixels[m+3])continue;
            const int c=mask->pixels[m]>0 ? 0 : mask->pixels[m+1]>0 ? 1 : -1;
            if(c<0||ops[c]==0)continue;
            const auto& tile=*textiles[c];
            for(int k=0;k<3;++k) {
                const auto color=tile.pixels.empty() ? static_cast<unsigned char>((ops[c]>>24U)==1 ? (ops[c]>>(16-8*k))&255U : 0) : tile.pixels[static_cast<std::size_t>((y%tile.height*tile.width+x%tile.width)*4+k)];
                dyed_source.pixels[dst+static_cast<std::size_t>(k)]=static_cast<unsigned char>((static_cast<unsigned>(color)*mask->pixels[m+static_cast<std::size_t>(c)]+127U)/255U);
            }
            dyed_source.pixels[dst+3]=255;
        }
        input=&dyed_source;
    }
    // Raster bounds protect against hostile size stats; density tracks the
    // reduction so world placement still preserves the authoritative size.
    const double factor=5*std::max(1,dyed ? size/5*5 : size)/100.0;
    const int wanted_w=std::max(1,static_cast<int>(factor*source.width)),wanted_h=std::max(1,static_cast<int>(factor*source.height));
    const double density=std::min(1.0,1024.0/std::max(wanted_w,wanted_h));
    const int w=std::max(1,static_cast<int>(wanted_w*density)),h=std::max(1,static_cast<int>(wanted_h*density));
    Image result{w+24,h+(pad?24:13),std::vector<unsigned char>(static_cast<std::size_t>(w+24)*static_cast<std::size_t>(h+(pad?24:13))*4)};result.pixel_scale=density;
    // Same arithmetic as compose_reference, with per-column and per-row terms computed once instead of once per pixel.
    struct Span { int first{},last{};double extent{};std::size_t offset{}; };
    const auto spans=[&](int count,int from,int source_count,std::vector<Span>& out,std::vector<double>& weights) {
        out.resize(static_cast<std::size_t>(count));weights.clear();
        for(int i=0;i<count;++i) {
            const double s0=count>=source_count ? std::floor((i+.49999)*source_count/count) : static_cast<double>(i)*source_count/count;
            const double s1=count>=source_count ? s0+1 : static_cast<double>(i+1)*source_count/count;
            auto& span=out[static_cast<std::size_t>(i)];span.first=static_cast<int>(s0);span.last=std::min(source_count,static_cast<int>(std::ceil(s1)));span.extent=s1-s0;span.offset=weights.size();
            for(int t=span.first;t<span.last;++t)weights.push_back(std::min(s1,t+1.0)-std::max(s0,static_cast<double>(t)));
        }
        (void)from;
    };
    static thread_local std::vector<Span> column_spans,row_spans;static thread_local std::vector<double> column_weights,row_weights;
    spans(w,input->width,input->width,column_spans,column_weights);spans(h,input->height,input->height,row_spans,row_weights);
    for(int y=0;y<h;++y) {
        const auto& ry=row_spans[static_cast<std::size_t>(y)];
        const int subtract=static_cast<int>(40*std::max(0.0,2.0*(y+12)/result.height-1));
        for(int x=0;x<w;++x) {
            const auto& rx=column_spans[static_cast<std::size_t>(x)];
            const double area=rx.extent*ry.extent;
            double channels[4]={};
            for(int sy=ry.first;sy<ry.last;++sy) {
                const double wy=row_weights[ry.offset+static_cast<std::size_t>(sy-ry.first)];
                for(int sx=rx.first;sx<rx.last;++sx) {
                    const double weight=column_weights[rx.offset+static_cast<std::size_t>(sx-rx.first)]*wy/area;
                    const auto src=static_cast<std::size_t>((sy*input->width+sx)*4);const double alpha=input->pixels[src+3]/255.0;
                    for(int k=0;k<3;++k)channels[k]+=input->pixels[src+static_cast<std::size_t>(k)]*alpha*weight;
                    channels[3]+=input->pixels[src+3]*weight;
                }
            }
            const auto dst=static_cast<std::size_t>(((y+12)*result.width+x+12)*4);
            if(channels[3]>0)for(int k=0;k<3;++k)result.pixels[dst+static_cast<std::size_t>(k)]=static_cast<unsigned char>(std::clamp(round_up(channels[k]*255/channels[3])-subtract,0L,255L));
            result.pixels[dst+3]=static_cast<unsigned char>(std::min(round_up(channels[3]),255L));
        }
    }
    // Black silhouette outline and a soft halo; original pixels stay crisp.
    const int rw=result.width,rh=result.height;const auto count=static_cast<std::size_t>(rw)*static_cast<std::size_t>(rh);
    static thread_local std::vector<unsigned char> alpha_plane,row_max,outline_plane;static thread_local std::vector<double> halo,next;
    alpha_plane.resize(count);row_max.resize(count);outline_plane.resize(count);halo.resize(count);next.resize(count);
    for(std::size_t i=0;i<count;++i) { alpha_plane[i]=result.pixels[i*4+3];halo[i]=alpha_plane[i]; }
    // Two seven-pixel box passes per axis form the separable triangular halo.
    for(int pass=0;pass<4;++pass) {
        const bool vertical=pass>=2;
        const int rows=vertical ? rw : rh,columns=vertical ? rh : rw;
        const auto at=[&](int row,int column) { return static_cast<std::size_t>(vertical ? column*rw+row : row*rw+column); };
        for(int row=0;row<rows;++row) {
            double sum=0;for(int c=0;c<std::min(3,columns);++c)sum+=halo[at(row,c)];
            for(int c=0;c<columns;++c) {
                if(c+3<columns)sum+=halo[at(row,c+3)];
                if(c-4>=0)sum-=halo[at(row,c-4)];
                next[at(row,c)]=sum/7;
            }
        }
        halo.swap(next);
    }
    // 3x3 maximum of the original alpha as two 1-D passes (exact for a rectangle clipped to the image).
    for(int y=0;y<rh;++y)for(int x=0;x<rw;++x) {
        unsigned char m=alpha_plane[static_cast<std::size_t>(y*rw+x)];
        if(x>0)m=std::max(m,alpha_plane[static_cast<std::size_t>(y*rw+x-1)]);
        if(x+1<rw)m=std::max(m,alpha_plane[static_cast<std::size_t>(y*rw+x+1)]);
        row_max[static_cast<std::size_t>(y*rw+x)]=m;
    }
    for(int y=0;y<rh;++y)for(int x=0;x<rw;++x) {
        unsigned char m=row_max[static_cast<std::size_t>(y*rw+x)];
        if(y>0)m=std::max(m,row_max[static_cast<std::size_t>((y-1)*rw+x)]);
        if(y+1<rh)m=std::max(m,row_max[static_cast<std::size_t>((y+1)*rw+x)]);
        outline_plane[static_cast<std::size_t>(y*rw+x)]=m;
    }
    for(std::size_t i=0;i<count;++i) {
        const auto dst=i*4;
        const double a=alpha_plane[i]/255.0,b=std::max(outline_plane[i]*.8,halo[i]*.6)/255.0;
        const double out=a+b*(1-a);result.pixels[dst+3]=static_cast<unsigned char>(std::min(round_up(out*255),255L));
        if(out>0)for(int k=0;k<3;++k)result.pixels[dst+static_cast<std::size_t>(k)]=static_cast<unsigned char>(round_up(result.pixels[dst+static_cast<std::size_t>(k)]*a/out));
    }
    return result;
}
Image Appearances::compose_reference(const Image& source,const Image* mask,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const {
    // The original implementation, kept verbatim as the differential oracle for `compose`.
    // Flash does texturing on a 5x bitmap before size scaling. Textile origin is
    // the frame origin, including transparent extended-attack padding.
    const bool dyed=mask&&(tex1||tex2);
    Image dyed_source;
    const Image* input=&source;
    if(dyed) {
        dyed_source={source.width*5,source.height*5,std::vector<unsigned char>(static_cast<std::size_t>(source.width*5)*static_cast<std::size_t>(source.height*5)*4)};
        Image textiles[2];const std::uint32_t ops[2]={tex1,tex2};
        for(int channel=0;channel<2;++channel) {
            const auto kind=ops[channel]>>24U;
            if(kind==4||kind==5||kind==9||kind==10) {
                const auto name="textile"+std::to_string(kind)+"x"+std::to_string(kind);
                for(std::size_t sheet=0;sheet<flash::sheets.size();++sheet)if(flash::sheets[sheet].name==name)textiles[channel]=sprite(static_cast<int>(sheet),static_cast<int>(ops[channel]&0xffffffU));
            }
        }
        for(int y=0;y<dyed_source.height;++y)for(int x=0;x<dyed_source.width;++x) {
            const auto src=static_cast<std::size_t>((y/5*source.width+x/5)*4),dst=static_cast<std::size_t>((y*dyed_source.width+x)*4);
            std::copy_n(source.pixels.data()+src,4,dyed_source.pixels.data()+dst);
            if(x/5>=mask->width||y/5>=mask->height)continue;
            const auto m=static_cast<std::size_t>((y/5*mask->width+x/5)*4);
            if(!mask->pixels[m+3])continue;
            const int c=mask->pixels[m]>0 ? 0 : mask->pixels[m+1]>0 ? 1 : -1;
            if(c<0||ops[c]==0)continue;
            const auto& tile=textiles[c];
            for(int k=0;k<3;++k) {
                const auto color=tile.pixels.empty() ? static_cast<unsigned char>((ops[c]>>24U)==1 ? (ops[c]>>(16-8*k))&255U : 0) : tile.pixels[static_cast<std::size_t>((y%tile.height*tile.width+x%tile.width)*4+k)];
                dyed_source.pixels[dst+static_cast<std::size_t>(k)]=static_cast<unsigned char>((static_cast<unsigned>(color)*mask->pixels[m+static_cast<std::size_t>(c)]+127U)/255U);
            }
            dyed_source.pixels[dst+3]=255;
        }
        input=&dyed_source;
    }
    // Raster bounds protect against hostile size stats; density tracks the
    // reduction so world placement still preserves the authoritative size.
    const double factor=5*std::max(1,dyed ? size/5*5 : size)/100.0;
    const int wanted_w=std::max(1,static_cast<int>(factor*source.width)),wanted_h=std::max(1,static_cast<int>(factor*source.height));
    const double density=std::min(1.0,1024.0/std::max(wanted_w,wanted_h));
    const int w=std::max(1,static_cast<int>(wanted_w*density)),h=std::max(1,static_cast<int>(wanted_h*density));
    Image result{w+24,h+(pad?24:13),std::vector<unsigned char>(static_cast<std::size_t>(w+24)*static_cast<std::size_t>(h+(pad?24:13))*4)};result.pixel_scale=density;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const auto dst=static_cast<std::size_t>(((y+12)*result.width+x+12)*4);
        const double x0=w>=input->width ? std::floor((x+.49999)*input->width/w) : static_cast<double>(x)*input->width/w;
        const double x1=w>=input->width ? x0+1 : static_cast<double>(x+1)*input->width/w;
        const double y0=h>=input->height ? std::floor((y+.49999)*input->height/h) : static_cast<double>(y)*input->height/h;
        const double y1=h>=input->height ? y0+1 : static_cast<double>(y+1)*input->height/h;
        double channels[4]={};
        for(int sy=static_cast<int>(y0);sy<std::min(input->height,static_cast<int>(std::ceil(y1)));++sy)
            for(int sx=static_cast<int>(x0);sx<std::min(input->width,static_cast<int>(std::ceil(x1)));++sx) {
                const double weight=(std::min(x1,sx+1.0)-std::max(x0,static_cast<double>(sx)))*(std::min(y1,sy+1.0)-std::max(y0,static_cast<double>(sy)))/((x1-x0)*(y1-y0));
                const auto src=static_cast<std::size_t>((sy*input->width+sx)*4);const double alpha=input->pixels[src+3]/255.0;
                for(int k=0;k<3;++k)channels[k]+=input->pixels[src+static_cast<std::size_t>(k)]*alpha*weight;
                channels[3]+=input->pixels[src+3]*weight;
            }
        const int subtract=static_cast<int>(40*std::max(0.0,2.0*(y+12)/result.height-1));
        if(channels[3]>0)for(int k=0;k<3;++k)result.pixels[dst+static_cast<std::size_t>(k)]=static_cast<unsigned char>(std::clamp(std::lround(channels[k]*255/channels[3])-subtract,0L,255L));
        result.pixels[dst+3]=static_cast<unsigned char>(std::clamp(std::lround(channels[3]),0L,255L));
    }
    // Black silhouette outline and a soft halo; original pixels stay crisp.
    const auto original=result;
    std::vector<double> halo(static_cast<std::size_t>(result.width)*static_cast<std::size_t>(result.height));
    for(std::size_t i=0;i<halo.size();++i)halo[i]=original.pixels[i*4+3];
    // Two seven-pixel box passes per axis form the separable triangular halo.
    for(int pass=0;pass<4;++pass) {
        std::vector<double> next(halo.size());const bool vertical=pass>=2;
        const int rows=vertical ? result.width : result.height,columns=vertical ? result.height : result.width;
        const auto at=[&](int row,int column) { return static_cast<std::size_t>(vertical ? column*result.width+row : row*result.width+column); };
        for(int row=0;row<rows;++row) {
            double sum=0;for(int c=0;c<std::min(3,columns);++c)sum+=halo[at(row,c)];
            for(int c=0;c<columns;++c) {
                if(c+3<columns)sum+=halo[at(row,c+3)];
                if(c-4>=0)sum-=halo[at(row,c-4)];
                next[at(row,c)]=sum/7;
            }
        }
        halo=std::move(next);
    }
    for(int y=0;y<result.height;++y)for(int x=0;x<result.width;++x) {
        const auto dst=static_cast<std::size_t>((y*result.width+x)*4);int outline=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int sx=x+dx,sy=y+dy;if(sx<0||sy<0||sx>=result.width||sy>=result.height)continue;
            outline=std::max(outline,static_cast<int>(original.pixels[static_cast<std::size_t>((sy*result.width+sx)*4+3)]));
        }
        const double a=original.pixels[dst+3]/255.0,b=std::max(outline*.8,halo[dst/4]*.6)/255.0;
        const double out=a+b*(1-a);result.pixels[dst+3]=static_cast<unsigned char>(std::clamp(std::lround(out*255),0L,255L));
        if(out>0)for(int k=0;k<3;++k)result.pixels[dst+static_cast<std::size_t>(k)]=static_cast<unsigned char>(std::lround(original.pixels[dst+static_cast<std::size_t>(k)]*a/out));
    }
    return result;
}
std::size_t Appearances::KeyHash::operator()(const Key& key) const noexcept {
    std::uint64_t h=1469598103934665603ULL;
    const auto mix=[&](std::uint64_t v) { h^=v+0x9e3779b97f4a7c15ULL+(h<<6U)+(h>>2U); };
    mix(static_cast<std::uint32_t>(key.a));mix(static_cast<std::uint32_t>(key.b));mix(static_cast<std::uint32_t>(key.c));mix(static_cast<std::uint32_t>(key.d));
    mix(static_cast<std::uint32_t>(key.size));mix(key.tex1);mix(key.tex2);mix(static_cast<std::uint64_t>(key.kind)<<8U|key.pad);
    return static_cast<std::size_t>(h*0x2545F4914F6CDD1DULL);
}
template <class Make> std::shared_ptr<const Image> Appearances::get(const Key& key,Make&& make) const {
    REALM_ZONE("appearance.lookup");
    if(const auto it=cache_.find(key);it!=cache_.end()) { telemetry::add(telemetry::Counter::appearance_hits); lru_.splice(lru_.begin(),lru_,it->second.lru);return it->second.image; }
    Image owned_source,owned_mask;const Image* source=nullptr;const Image* mask=nullptr;bool normalize=false;
    make(owned_source,owned_mask,source,mask,normalize);
    Image normalized_source,normalized_mask;
    const Image* input=source;
    if(normalize) { normalized_source=*source;normalize_flash_png(normalized_source);input=&normalized_source;if(mask) { normalized_mask=*mask;normalize_flash_png(normalized_mask);mask=&normalized_mask; } }
    telemetry::add(telemetry::Counter::appearance_misses);
    auto image=std::make_shared<const Image>(compose(*input,mask,key.size,key.pad,key.tex1,key.tex2));
    while(!cache_.empty()&&(cache_.size()>=budget_entries_||bytes_+image->pixels.size()>budget_bytes_)) {
        const auto it=cache_.find(lru_.back());bytes_-=it->second.image->pixels.size();cache_.erase(it);lru_.pop_back();++evictions_;telemetry::add(telemetry::Counter::appearance_evictions);
    }
    bytes_+=image->pixels.size();lru_.push_front(key);cache_.emplace(key,Entry{image,lru_.begin()});return image;
}
void Appearances::set_budget(std::size_t entries,std::size_t bytes) const {
    budget_entries_=entries;budget_bytes_=bytes;
    while(!cache_.empty()&&(cache_.size()>budget_entries_||bytes_>budget_bytes_)) {
        const auto it=cache_.find(lru_.back());bytes_-=it->second.image->pixels.size();cache_.erase(it);lru_.pop_back();++evictions_;telemetry::add(telemetry::Counter::appearance_evictions);
    }
}
Appearances::Key Appearances::animated_key(AnimationFrame frame,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const {
    return {static_cast<std::int32_t>(frame.image),static_cast<std::int32_t>(frame.mask),0,0,size,tex1,tex2,1,static_cast<std::uint8_t>(pad)};
}
Appearances::Key Appearances::texture_key(const TextureDef& definition,std::uint32_t id,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const {
    const auto& t=definition.select(id);
    if(const auto* c=animations_.character(t.animated,t.index))return animated_key(c->frame(0,0,Action::stand,0),size,pad,tex1,tex2);
    return {t.sheet,t.index,t.mask_sheet,t.mask_index,size,tex1,tex2,0,static_cast<std::uint8_t>(pad)};
}
std::shared_ptr<const Image> Appearances::image(const Key& key) const {
    return get(key,[&](Image& source,Image& mask,const Image*& source_ref,const Image*& mask_ref,bool& normalize) {
        if(key.kind==1) {
            source_ref=&animations_.images().at(static_cast<std::size_t>(key.a));mask_ref=key.b<0 ? nullptr : &animations_.images().at(static_cast<std::size_t>(key.b));normalize=true;return;
        }
        source=sprite(key.a,key.b);source_ref=&source;
        if(key.c>=0) { mask=sprite(key.c,key.d);mask_ref=&mask; }
    });
}
std::shared_ptr<const Image> Appearances::animated(AnimationFrame frame,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const { return image(animated_key(frame,size,pad,tex1,tex2)); }
std::shared_ptr<const Image> Appearances::texture(const TextureDef& definition,std::uint32_t id,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const { return image(texture_key(definition,id,size,pad,tex1,tex2)); }
std::shared_ptr<const Image> Appearances::portrait(const Catalog& catalog,int type,int skin,std::uint32_t tex1,std::uint32_t tex2) const {
    const auto& t=catalog.player_texture(type,skin).select(0);
    if(const auto* c=animations_.character(t.animated,t.index)) {
        const auto frame=c->directions[0][0].front();return animated(frame,400/animations_.images().at(frame.image).width,true,tex1,tex2);
    }
    return texture(t,0,50,true,tex1,tex2);
}
void Appearances::clear() const { cache_.clear();lru_.clear();bytes_=0; }
}
