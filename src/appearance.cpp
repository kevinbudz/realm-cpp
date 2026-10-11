#include "realm/appearance.hpp"
#include "flash_assets.hpp"
#include <array>
#include <algorithm>
#include <cmath>
namespace realm {
std::array<std::uint32_t,2> world_dyes(const World& world,const Entity& entity) {
    std::array<std::uint32_t,2> result{entity.tex1,entity.tex2};
    if(!entity.player||entity.id!=world.player_id())return result;
    const int x=static_cast<int>(entity.position.x),y=static_cast<int>(entity.position.y);
    const int dx=entity.position.x-x>.5 ? 1 : -1,dy=entity.position.y-y>.5 ? 1 : -1;
    constexpr int nearby[4][2]={{0,0},{1,0},{0,1},{1,1}};
    for(const auto& offset:nearby)for(const auto& e:world.entities()) {
        const auto* d=world.catalog().object(e.type);
        if(!d||d->class_name!="Merchant"||static_cast<int>(e.position.x)!=x+dx*offset[0]||static_cast<int>(e.position.y)!=y+dy*offset[1])continue;
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
    return result;
}
Appearances::Appearances(const std::filesystem::path& root,const AnimationLibrary& animations):animations_(animations) {
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
Image Appearances::compose(const Image& source,const Image* mask,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const {
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
std::shared_ptr<const Image> Appearances::get(std::string key,const Image& source,const Image* mask,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2,bool normalize) const {
    key+=':'+std::to_string(size)+':'+std::to_string(pad)+':'+std::to_string(tex1)+':'+std::to_string(tex2);
    if(const auto it=cache_.find(key);it!=cache_.end()) { lru_.splice(lru_.begin(),lru_,it->second.lru);return it->second.image; }
    Image normalized_source,normalized_mask;
    const Image* input=&source;
    if(normalize) { normalized_source=source;normalize_flash_png(normalized_source);input=&normalized_source;if(mask) { normalized_mask=*mask;normalize_flash_png(normalized_mask);mask=&normalized_mask; } }
    auto image=std::make_shared<const Image>(compose(*input,mask,size,pad,tex1,tex2));
    while(!cache_.empty()&&(cache_.size()>=maximum_entries||bytes_+image->pixels.size()>maximum_bytes)) {
        const auto it=cache_.find(lru_.back());bytes_-=it->second.image->pixels.size();cache_.erase(it);lru_.pop_back();++evictions_;
    }
    bytes_+=image->pixels.size();lru_.push_front(key);cache_.emplace(key,Entry{image,lru_.begin()});return image;
}
std::shared_ptr<const Image> Appearances::animated(AnimationFrame frame,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const {
    const auto& source=animations_.images().at(frame.image);const auto* mask=frame.mask<0 ? nullptr : &animations_.images().at(static_cast<std::size_t>(frame.mask));
    return get("a"+std::to_string(frame.image)+"m"+std::to_string(frame.mask),source,mask,size,pad,tex1,tex2,true);
}
std::shared_ptr<const Image> Appearances::texture(const TextureDef& definition,std::uint32_t id,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const {
    const auto& t=definition.select(id);
    if(const auto* c=animations_.character(t.animated,t.index))return animated(c->frame(0,0,Action::stand,0),size,pad,tex1,tex2);
    const auto source=sprite(t.sheet,t.index);const auto mask=t.mask_sheet<0 ? Image{} : sprite(t.mask_sheet,t.mask_index);
    return get("s"+std::to_string(t.sheet)+"i"+std::to_string(t.index)+"m"+std::to_string(t.mask_sheet)+"i"+std::to_string(t.mask_index),source,t.mask_sheet<0?nullptr:&mask,size,pad,tex1,tex2);
}
std::shared_ptr<const Image> Appearances::portrait(const Catalog& catalog,int type,int skin,std::uint32_t tex1,std::uint32_t tex2) const {
    const auto& t=catalog.player_texture(type,skin).select(0);
    if(const auto* c=animations_.character(t.animated,t.index)) {
        const auto frame=c->directions[0][0].front();return animated(frame,400/animations_.images().at(frame.image).width,true,tex1,tex2);
    }
    return texture(t,0,50,true,tex1,tex2);
}
void Appearances::clear() const { cache_.clear();lru_.clear();bytes_=0; }
}
