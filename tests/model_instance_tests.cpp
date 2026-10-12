#include <GL/glew.h>
// Differential checks of instanced model rendering (model_quad + persistent model_geometry,
// projected, culled and shaded on the GPU) against the CPU reference path (project_model).
// CPU part: the instance transform reproduces every reference vertex, the kept-face set and
// the flat shade. GPU part: both paths render the same frames (every model, camera/object
// rotations, zooms, with and without sprites, overlapping painter-ordered scenes with
// interleaved sprites) and the captures are compared pixel by pixel.
#include "realm/animation.hpp"
#include "realm/catalog.hpp"
#include "realm/model.hpp"
#include "realm/renderer.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
namespace {
using namespace realm;
int checks{};
std::size_t slivers{};
void check(bool ok,const std::string& why) { ++checks;if(!ok)throw std::runtime_error(why); }
Camera camera_at(Vec2 at,double angle,double zoom,int w=800,int h=600) { Camera c;c.configure(at,angle,w,h,zoom,true);return c; }
constexpr double pi=3.14159265358979323846;

// CPU emulation of the model vertex shader on the quad's float fields.
struct Emulated { std::vector<MeshVertex> kept;std::vector<bool> textured; };
Emulated emulate(const Quad& q) {
    Emulated r;const auto range=model_range(static_cast<std::uint32_t>(q.offset[2]));
    const auto geometry=model_geometry().subspan(range.first,range.count);
    for(std::size_t t=0;t+2<geometry.size();t+=3) {
        Vec2 p[3];
        for(int k=0;k<3;++k) { const auto& v=geometry[t+k];
            p[k]={q.origin[0]+double(q.axis_x[0])*v.x+double(q.axis_y[0])*v.y+double(q.uv_offset[0])*v.z,
                  q.origin[1]+double(q.axis_x[1])*v.x+double(q.axis_y[1])*v.y+double(q.uv_offset[1])*v.z}; }
        // Stored fan order (i, 0, i+1): kept when the reference winding test is positive.
        if((p[0].x-p[1].x)*(p[2].y-p[1].y)-(p[0].y-p[1].y)*(p[2].x-p[1].x)<=0)continue;
        const auto& n=geometry[t];const float c=q.offset[0],s=q.offset[1];
        const float nx=n.nx*c-n.ny*s,ny=n.nx*s+n.ny*c;
        const float lit=.75F+.25F*std::max(0.0F,(nx+3*ny+2*n.nz)/3.7416573867739413F);
        for(int k=0;k<3;++k)r.kept.push_back({float(p[k].x),float(p[k].y),geometry[t+k].u,geometry[t+k].v,lit});
        r.textured.push_back(geometry[t].solid<.5F&&q.multiply[3]>.5F);
    }
    return r;
}
double area(const MeshVertex* v) { return std::abs((v[0].x-v[1].x)*(v[2].y-v[1].y)-(v[0].y-v[1].y)*(v[2].x-v[1].x)); }
// Height of the triangle over its longest edge, in pixels: an edge-on face is a sliver however long it is.
double thickness(const MeshVertex* v) {
    double edge=0;for(int k=0;k<3;++k)edge=std::max(edge,std::hypot(double(v[k].x)-v[(k+1)%3].x,double(v[k].y)-v[(k+1)%3].y));
    return edge>0?area(v)/edge:0;
}

// Compares one reference/instance pair. Faces whose kept state differs must be edge-on slivers.
void compare_cpu(const std::string& name,const Camera& camera,Vec2 at,double rotation,std::uint32_t color,const std::optional<SpriteRegion>& sprite) {
    const auto index=model_index(name);check(index.has_value(),"model index "+name);
    const auto ref=project_model(name,camera,at,rotation,color,sprite);const auto q=model_quad(*index,camera,at,rotation,color,sprite);
    check(q.kind==9&&!q.owner&&q.texture==0,"instance quad has no owner");
    const auto e=emulate(q);
    std::vector<MeshVertex> want;std::vector<bool> want_textured;
    if(ref)for(const auto& g:ref->groups)for(std::uint32_t i=g.first;i<g.first+g.count;i+=3) { for(int k=0;k<3;++k)want.push_back(ref->vertices[i+k]);want_textured.push_back(!g.solid); }
    if(ref)check(std::abs(ref->color[0]-q.multiply[0])<1e-7F&&std::abs(ref->color[1]-q.multiply[1])<1e-7F&&std::abs(ref->color[2]-q.multiply[2])<1e-7F&&ref->bounds==q.uv&&ref->page==q.page,"material fields "+name);
    // Walk both kept lists; the geometry is the same fan in the same order, so they align except for slivers.
    std::size_t a=0,b=0;
    while(a<want.size()||b<e.kept.size()) {
        const bool match=a<want.size()&&b<e.kept.size()&&std::abs(want[a].u-e.kept[b].u)==0&&std::abs(want[a].v-e.kept[b].v)==0&&
            std::abs(want[a].x-e.kept[b].x)<5e-3F&&std::abs(want[a].y-e.kept[b].y)<5e-3F&&std::abs(want[a+1].x-e.kept[b+1].x)<5e-3F&&std::abs(want[a+2].y-e.kept[b+2].y)<5e-3F;
        if(match) {
            for(int k=0;k<3;++k) { check(std::abs(want[a+k].x-e.kept[b+k].x)<5e-3F&&std::abs(want[a+k].y-e.kept[b+k].y)<5e-3F,"vertex position "+name);
                check(std::abs(want[a+k].shade-e.kept[b+k].shade)<2e-6F,"flat shade "+name); }
            check(want_textured[a/3]==e.textured[b/3],"material group "+name);
            a+=3;b+=3;continue;
        }
        // A face kept by only one side must have (near) zero screen area.
        if(a<want.size()&&thickness(&want[a])<1e-3) { a+=3;++slivers;continue; }
        if(b<e.kept.size()&&thickness(&e.kept[b])<1e-3) { b+=3;++slivers;continue; }
        if(std::getenv("MODEL_INSTANCE_DEBUG")) {
            std::cerr<<"ref "<<a<<"/"<<want.size()<<" inst "<<b<<"/"<<e.kept.size()<<" rotation "<<rotation<<" zoom "<<camera.scale()<<" angle "<<camera.angle()<<"\n";
            if(a<want.size())std::cerr<<" ref tri ("<<want[a].x<<","<<want[a].y<<") ("<<want[a+1].x<<","<<want[a+1].y<<") ("<<want[a+2].x<<","<<want[a+2].y<<") area "<<area(&want[a])<<"\n";
            if(b<e.kept.size())std::cerr<<" inst tri ("<<e.kept[b].x<<","<<e.kept[b].y<<") ("<<e.kept[b+1].x<<","<<e.kept[b+1].y<<") ("<<e.kept[b+2].x<<","<<e.kept[b+2].y<<") area "<<area(&e.kept[b])<<"\n";
        }
        throw std::runtime_error("kept faces diverge for "+name);
    }
}
}
int main() { try {
    // Index, geometry and range bookkeeping.
    std::size_t vertices{};
    for(std::size_t i=0;i<model_count();++i) {
        const auto name=model_name(i);const auto index=model_index(name);
        check(index&&*index==i,"model_index round trip");
        const auto range=model_range(static_cast<std::uint32_t>(i));
        check(range.first==vertices&&range.count==3*model_triangle_count(name),"contiguous model ranges");vertices+=range.count;
    }
    check(vertices==model_geometry().size()&&!model_index("Nonexistent")&&model_range(9999).count==0,"geometry covers every model");
    // CPU differential: every model, 24 camera angles x object rotations x zooms, with and without a sprite.
    const SpriteRegion sprite{1,16,32,8,8};std::mt19937 rng(9);std::uniform_real_distribution<double> unit(0,1);
    std::size_t cases{};
    for(std::size_t i=0;i<model_count();++i)for(int a=0;a<24;++a)for(const double zoom:{1.0,2.0,.75})for(int r=0;r<4;++r) {
        const Vec2 at{30+unit(rng)*40,30+unit(rng)*40};const double rotation=r==0?0:r==1?90:r==2?a*15.0:unit(rng)*720-360;
        const auto camera=camera_at({at.x+unit(rng)*8-4,at.y+unit(rng)*6-3},a*pi/12+(r==3?unit(rng):0),zoom);
        compare_cpu(std::string(model_name(i)),camera,at,rotation,0x336699,(a+r)%2?std::optional<SpriteRegion>{sprite}:std::nullopt);++cases;
    }
    std::cout<<"CPU instance/reference cases: "<<cases<<", edge-on faces (<0.001 px thick) kept by one side only: "<<slivers<<"\n";

    // GPU differential.
    SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    constexpr int width=800,height=600;
    auto* window=SDL_CreateWindow("model instances",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);if(!window)throw std::runtime_error(SDL_GetError());
    auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
    glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)throw std::runtime_error("GLEW");while(glGetError()!=GL_NO_ERROR){}
    std::size_t frames{},drawn{},differing{},large{},instances{};
    {
        Catalog catalog(flash::source_root);AnimationLibrary animations(flash::source_root);
        Atlas atlas(flash::source_root,animations.images(),animations.images().size());
        Renderer renderer(65536);renderer.clear_color({54/255.F,54/255.F,54/255.F,1});
        // One real object (and its real sprite) per model.
        std::map<std::string,const ObjectDef*> objects;
        for(const auto& d:catalog.objects())if(!d.model.empty()&&has_model(d.model)&&!objects.contains(d.model))objects.emplace(d.model,&d);
        std::vector<SpriteRegion> sprites;
        for(const auto& d:catalog.objects())if(d.model.empty()&&d.texture.sheet>=0&&sprites.size()<64)sprites.push_back(atlas.sprite(static_cast<std::size_t>(d.texture.sheet),d.texture.index));
        auto material=[&](const ObjectDef& d) { std::optional<SpriteRegion> s;const auto& t=d.texture.select(0);if(t.sheet>=0)s=atlas.sprite(static_cast<std::size_t>(t.sheet),t.index);return s; };
        struct Item { const ObjectDef* def;Vec2 at;double rotation;std::optional<SpriteRegion> sprite;int depth; };
        // Renders the same painter stream twice, models as reference meshes and as instances, and compares.
        auto run=[&](const Camera& camera,const std::vector<Item>& items,bool interleave_sprites,const std::string& label) {
            std::vector<Quad> ref,inst;int k=0;
            for(const auto& item:items) {
                if(interleave_sprites&&!sprites.empty()) {
                    const auto p=camera.world_to_screen(item.at);const auto& s=sprites[static_cast<std::size_t>(k++)%sprites.size()];
                    const auto q=sprite_quad(s,{p.x-20,p.y-30},{40,0},{0,40});ref.push_back(q);inst.push_back(q);
                }
                if(auto mesh=project_model(item.def->model,camera,item.at,item.rotation,item.def->color,item.sprite))ref.push_back(mesh_quad(std::move(mesh)));
                inst.push_back(model_quad(*model_index(item.def->model),camera,item.at,item.rotation,item.def->color,item.sprite));++instances;
            }
            renderer.draw(atlas,ref,width,height,width);const auto a=renderer.capture(width,height);
            renderer.draw(atlas,inst,width,height,width);const auto b=renderer.capture(width,height);
            std::size_t diff{},big{},covered{};
            for(std::size_t p=0;p<a.pixels.size();p+=4) {
                int delta=0;bool background=true;
                for(int c=0;c<3;++c) { delta=std::max(delta,std::abs(int(a.pixels[p+c])-int(b.pixels[p+c])));background&=a.pixels[p+c]==54; }
                covered+=!background;diff+=delta>0;big+=delta>16;
            }
            ++frames;drawn+=covered;differing+=diff;large+=big;
            // Edge rounding may flip a handful of boundary pixels; anything more is a transform/cull/material error.
            if(big>4+covered/20000) { std::cerr<<label<<": "<<big<<" of "<<covered<<" pixels differ by >16\n";throw std::runtime_error("instance render differs from reference: "+label); }
            ++checks;
        };
        for(const auto& [model,def]:objects)for(const int a:{0,5,9,17})for(const double zoom:{1.0,2.0}) {
            const auto camera=camera_at({50,50},a*pi/12,zoom);std::vector<Item> items;
            for(int y=0;y<3;++y)for(int x=0;x<4;++x)items.push_back({def,{48.2+x*1.2/zoom,48.8+y*1.2/zoom},(x+4*y)*33.0,(x+y)%3?material(*def):std::nullopt,0});
            run(camera,items,false,model+" angle "+std::to_string(a)+" zoom "+std::to_string(zoom));
        }
        // Overlapping varied scenes in painter (depth, id) order with sprites interleaved, as Scene submits them.
        std::vector<const ObjectDef*> pool;for(const auto& [m,d]:objects)pool.push_back(d);
        std::uniform_int_distribution<std::size_t> pick(0,pool.size()-1);
        for(int scene=0;scene<24;++scene) {
            const auto camera=camera_at({50,50},unit(rng)*2*pi,scene%3==2?2.0:1.0);std::vector<Item> items;
            for(int n=0;n<60;++n) { const auto* d=pool[pick(rng)];const Vec2 at{46+unit(rng)*8,47+unit(rng)*6};
                items.push_back({d,at,unit(rng)*360,unit(rng)<.7?material(*d):std::nullopt,static_cast<int>(camera.world_to_screen(at).y)}); }
            std::stable_sort(items.begin(),items.end(),[](const Item& x,const Item& y){return x.depth<y.depth;});
            run(camera,items,scene%2==0,"overlap scene "+std::to_string(scene));
        }
    }
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"GPU frames "<<frames<<", instances "<<instances<<", covered pixels "<<drawn<<", differing "<<differing<<" (by >16: "<<large<<")\n";
    std::cout<<"Model instance checks: "<<checks<<" passed\n";return 0;
} catch(const std::exception& e) { std::cerr<<"Model instance test failed: "<<e.what()<<"\n";return 1; } }
