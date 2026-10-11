#include "realm/movement.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace realm {
namespace {
constexpr std::uint32_t weak=1U<<1U, slowed=1U<<2U, dazed=1U<<4U, confused=1U<<9U,
    paralyzed=1U<<12U, speedy=1U<<13U, damaging=1U<<16U, berserk=1U<<17U,
    invincible=1U<<20U, invulnerable=1U<<21U, armored=1U<<22U, armor_broken=1U<<23U;
bool full(const World& world, Vec2 pos) {
    const auto* tile = world.tile(static_cast<int>(pos.x),static_cast<int>(pos.y));
    if (!tile || tile->type == 255) return true;
    const auto* object = world.entity(tile->object_id); const auto* def = object ? world.catalog().object(object->type) : nullptr;
    return def && def->full_occupy;
}
Vec2 step(const World& world, const Entity& player, Vec2 desired) {
    const auto start = player.position;
    const bool x_cross = (std::fmod(start.x,.5) == 0 && desired.x != start.x) || static_cast<int>(start.x / .5) != static_cast<int>(desired.x / .5);
    const bool y_cross = (std::fmod(start.y,.5) == 0 && desired.y != start.y) || static_cast<int>(start.y / .5) != static_cast<int>(desired.y / .5);
    if ((!x_cross && !y_cross) || valid_player_position(world,player,desired)) return desired;
    Vec2 border{};
    if (x_cross) { border.x = desired.x > start.x ? static_cast<int>(desired.x*2)/2.0 : static_cast<int>(start.x*2)/2.0; if (static_cast<int>(border.x) > static_cast<int>(start.x)) border.x -= .01; }
    if (y_cross) { border.y = desired.y > start.y ? static_cast<int>(desired.y*2)/2.0 : static_cast<int>(start.y*2)/2.0; if (static_cast<int>(border.y) > static_cast<int>(start.y)) border.y -= .01; }
    if (!x_cross) return {desired.x,border.y};
    if (!y_cross) return {border.x,desired.y};
    const Vec2 along_x{desired.x,border.y}, along_y{border.x,desired.y};
    const bool prefer_x = std::abs(desired.x-border.x) > std::abs(desired.y-border.y);
    const auto first = prefer_x ? along_x : along_y, second = prefer_x ? along_y : along_x;
    if (valid_player_position(world,player,first)) return first;
    if (valid_player_position(world,player,second)) return second;
    return border;
}
}
double move_speed(const Entity& p) {
    if (p.condition & slowed) return .004 * p.move_multiplier;
    double speed = .004 + p.speed / 75.0 * (.0096-.004);
    if (p.condition & speedy) speed *= 1.5;
    return speed * p.move_multiplier;
}
double attack_frequency(const Entity& p) {
    if (p.condition & dazed) return .0015;
    const double freq = .0015 + p.dexterity / 75.0 * (.008-.0015);
    return (p.condition & berserk) ? freq*1.5 : freq;
}
double attack_multiplier(const Entity& p) {
    if (p.condition & weak) return .5;
    const double multiplier = .5 + p.attack / 75.0 * 1.5;
    return (p.condition & damaging) ? multiplier*1.5 : multiplier;
}
int damage_with_defense(int damage, int defense, bool piercing, std::uint32_t condition) {
    if (condition & invulnerable) return 0;
    if (piercing || (condition & armor_broken)) defense=0;
    else if (condition & armored) defense*=2;
    return std::max(damage*3/20,damage-defense);
}
bool valid_player_position(const World& world, const Entity& player, Vec2 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    const auto* current = world.tile(static_cast<int>(player.position.x),static_cast<int>(player.position.y));
    const auto* next = p.x < 0 || p.y < 0 || p.x >= world.width() || p.y >= world.height() ? nullptr : world.tile(static_cast<int>(p.x),static_cast<int>(p.y));
    if (current != next && !world.walkable(p.x,p.y)) return false;
    const double xf=p.x-static_cast<int>(p.x), yf=p.y-static_cast<int>(p.y);
    if (xf < .5) {
        if (full(world,{p.x-1,p.y})) return false;
        if (yf < .5 && (full(world,{p.x,p.y-1}) || full(world,{p.x-1,p.y-1}))) return false;
        if (yf > .5 && (full(world,{p.x,p.y+1}) || full(world,{p.x-1,p.y+1}))) return false;
    } else if (xf > .5) {
        if (full(world,{p.x+1,p.y})) return false;
        if (yf < .5 && (full(world,{p.x,p.y-1}) || full(world,{p.x+1,p.y-1}))) return false;
        if (yf > .5 && (full(world,{p.x,p.y+1}) || full(world,{p.x+1,p.y+1}))) return false;
    } else {
        if (yf < .5 && full(world,{p.x,p.y-1})) return false;
        if (yf > .5 && full(world,{p.x,p.y+1})) return false;
    }
    return true;
}
Vec2 modify_player_move(const World& world, const Entity& player, Vec2 desired) {
    if (!std::isfinite(desired.x) || !std::isfinite(desired.y) || (player.condition & paralyzed)) return player.position;
    const double dx=desired.x-player.position.x,dy=desired.y-player.position.y;
    if (std::abs(dx) < .4 && std::abs(dy) < .4) return step(world,player,desired);
    double size=.4/std::max(std::abs(dx),std::abs(dy)), done{}; auto result=player.position;
    while (done < 1) { if (done+size >= 1) size=1-done; result=step(world,player,{result.x+dx*size,result.y+dy*size}); done+=size; }
    return result;
}
void move_player(World& world, Entity& player, Vec2 input, double rotation, double dt, double& angle) {
    if (!std::isfinite(dt) || dt < 0 || dt > 1000) throw std::invalid_argument("Invalid player frame interval");
    if (player.condition & confused) { input={-input.y,-input.x}; rotation=-rotation; }
    angle += dt*.003*rotation;
    player.velocity={};
    if (input.x || input.y) { const double facing=angle+std::atan2(input.y,input.x), speed=move_speed(player); player.velocity={speed*std::cos(facing),speed*std::sin(facing)}; }
    player.velocity.x-=player.push.x;player.velocity.y-=player.push.y;
    player.position=modify_player_move(world,player,{player.position.x+dt*player.velocity.x,player.position.y+dt*player.velocity.y});
    if (player.velocity.x || player.velocity.y) player.facing=std::atan2(player.velocity.y,player.velocity.x);
}
void player_move_ack(World& world, Entity& player, std::int32_t time) {
    const auto* tile=world.tile(static_cast<int>(player.position.x),static_cast<int>(player.position.y));const auto* ground=tile ? world.catalog().ground(tile->type) : nullptr;
    if (!ground) return;
    if (ground->sinking) { player.sink_level=std::min(player.sink_level+1,18);player.move_multiplier=.1+(1-player.sink_level/18.0)*(ground->speed-.1); }
    else { player.sink_level=0;player.move_multiplier=ground->speed; }
    const auto* object=world.entity(tile->object_id);const auto* def=object ? world.catalog().object(object->type) : nullptr;
    if (ground->damage && !(player.condition&invincible) && (!def || !def->protect_ground)) world.report_damage(player.id,ground->damage,time);
    player.push=ground->push ? Vec2{ground->animate_x/1000,ground->animate_y/1000} : Vec2{};
}
}
