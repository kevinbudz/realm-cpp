#include "realm/ui_draw.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
std::vector<Vec2> cut_edge_rect(double x,double y,double w,double h,double c,std::array<bool,4> k) {
    std::vector<Vec2> path;
    if(k[0]) { path.push_back({x,y+c});path.push_back({x+c,y}); } else path.push_back({x,y});
    if(k[1]) { path.push_back({x+w-c,y});path.push_back({x+w,y+c}); } else path.push_back({x+w,y});
    if(k[2]) { path.push_back({x+w,y+h-c});path.push_back({x+w-c,y+h}); } else path.push_back({x+w,y+h});
    if(k[3]) { path.push_back({x+c,y+h});path.push_back({x,y+h-c}); } else path.push_back({x,y+h});
    return path;
}

void ui_path(Image& image,std::span<const Vec2> path,std::optional<std::uint32_t> fill,std::optional<std::uint32_t> stroke,
             double stroke_width,double fill_alpha,double clip_right) {
    if(path.size()<2)return;
    const double s=image.pixel_scale,half=stroke ? stroke_width/2 : 0;
    double left=1e9,top=1e9,right=-1e9,bottom=-1e9;
    for(const auto p:path) { left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y); }
    right=std::min(right,clip_right);
    const int x0=std::max(0,static_cast<int>(std::floor((left-half-1)*s))),x1=std::min(image.width,static_cast<int>(std::ceil((right+half+1)*s)));
    const int y0=std::max(0,static_cast<int>(std::floor((top-half-1)*s))),y1=std::min(image.height,static_cast<int>(std::ceil((bottom+half+1)*s)));
    for(int py=y0;py<y1;++py)for(int px=x0;px<x1;++px) {
        const Vec2 p{(px+.5)/s,(py+.5)/s};
        if(p.x>clip_right)continue;
        bool inside=false;double distance=1e9;
        for(std::size_t i=0,j=path.size()-1;i<path.size();j=i++) {
            const auto a=path[i],b=path[j];
            if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
            const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
            const double t=length>0 ? std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.0,1.0) : 0;
            distance=std::min(distance,std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy));
        }
        auto* pixel=&image.pixels[(static_cast<std::size_t>(py)*static_cast<std::size_t>(image.width)+static_cast<std::size_t>(px))*4];
        const auto over=[&](std::uint32_t color,double alpha) {
            if(alpha<=0)return;
            for(unsigned c=0;c<3;++c)pixel[c]=static_cast<unsigned char>(std::lround(((color>>(16-8*c))&255U)*alpha+pixel[c]*(1-alpha)));
            pixel[3]=static_cast<unsigned char>(std::lround(alpha*255+pixel[3]*(1-alpha)));
        };
        if(fill)over(*fill,std::clamp(.5+(inside ? distance : -distance)*s,0.0,1.0)*fill_alpha);
        if(stroke)over(*stroke,std::clamp((half-distance)*s+.5,0.0,1.0));
    }
}
void ui_menu_backdrop(Image& image,double logical_width) {
    const double scale=image.pixel_scale;
    for(int py=0;py<image.height;++py) {
        const double y=(py+.5)/scale,ratio=(y-508)*255/80;double alpha=0;
        if(ratio>=20&&ratio<103)alpha=.73*(ratio-20)/83;
        else if(ratio>=103&&ratio<173)alpha=.73;
        else if(ratio>=173&&ratio<237)alpha=.73*(237-ratio)/64;
        const double combined=alpha+.8*(1-alpha);
        const auto value=static_cast<unsigned char>(std::lround(43*.8*(1-alpha)));
        for(int px=0;px<image.width;++px) {
            const auto at=(static_cast<std::size_t>(py)*static_cast<std::size_t>(image.width)+static_cast<std::size_t>(px))*4;
            image.pixels[at]=image.pixels[at+1]=image.pixels[at+2]=value;image.pixels[at+3]=static_cast<unsigned char>(std::lround(combined*255));
        }
    }
    ui_rectangle(image,0,524,logical_width,52,0x545454,true);
}
}
