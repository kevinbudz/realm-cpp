#include "realm/party.hpp"
#include "flash_assets.hpp"
#include <algorithm>
namespace realm {
namespace {
constexpr double party_distance_sq=50*50;
}
void Party::reset() { *this=Party{}; }
void Party::receive(const net::incoming::AccountList& list) {
    if(list.list_id==0)for(const auto id:list.account_ids) { starred_.insert(id);last_update_=std::numeric_limits<std::int32_t>::min(); }
    if(list.list_id==1) {
        ignored_.clear();
        for(const auto id:list.account_ids) { ignored_.insert(id);last_update_=std::numeric_limits<std::int32_t>::min(); }
    }
}
void Party::edit(std::int32_t account,std::int32_t object_id,bool ignore,bool add) {
    auto& list=ignore ? ignored_ : starred_;
    if(add)list.insert(account);
    else { list.erase(account);if(!ignore)starred_objects_.erase(object_id); }
    last_update_=std::numeric_limits<std::int32_t>::min();
}
void Party::update(const World& world,std::int32_t time) {
    if(time<last_update_+500)return;
    last_update_=time;members_.fill(-1);
    const auto* player=world.entity(world.player_id());
    if(!player)return;
    std::erase_if(starred_objects_,[&](std::int32_t id) { return !world.entity(id); });
    struct Candidate { bool starred{};double distance{};std::int32_t id{}; };
    std::vector<Candidate> candidates;
    for(const auto& other:world.entities()) {
        if(!other.player||other.id==player->id)continue;
        const auto account=other.stats.find(static_cast<std::uint8_t>(flash::StatId::ACCOUNT_ID_STAT));
        if(starred_.contains(account==other.stats.end() ? -1 : account->second))starred_objects_.insert(other.id);
        const bool starred=starred_objects_.contains(other.id);
        const double dx=player->position.x-other.position.x,dy=player->position.y-other.position.y,distance=dx*dx+dy*dy;
        if(!(distance>party_distance_sq&&!starred))candidates.push_back({starred,distance,other.id});
    }
    // sortOn(["starred_","distSqFromThisPlayer_","objectId_"]): starred first, then nearest, then lowest ID.
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
        if(a.starred!=b.starred)return a.starred;
        if(a.distance!=b.distance)return a.distance<b.distance;
        return a.id<b.id;
    });
    for(std::size_t i=0;i<members_.size()&&i<candidates.size();++i)members_[i]=candidates[i].id;
}
}
