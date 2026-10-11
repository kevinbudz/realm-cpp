#include "realm/hud_input.hpp"
#include <cmath>
namespace realm {
int HudInput::potion_at(Vec2 p,int selected) {
    if(selected==1)return -1;
    // The magic view is added last and is topmost. Icons are 46x35 at (13,-11)
    // and cover the lower grid row; potion_hud_tests.py verifies the baked size.
    for(int i=1;i>=0;--i) {
        const double x=p.x-potion_view_x[static_cast<std::size_t>(i)],y=p.y-potion_view_y;
        if(x>=13&&x<59&&y>=-11&&y<24)return i;
        if(x<0||x>=84||y<0||y>=24)continue;
        // PotionInventoryView cuts: left [1,0,0,1], right [0,1,1,0].
        if(i==0&&(x+y<4||x+24-y<4))continue;
        if(i==1&&(84-x+y<4||108-x-y<4))continue;
        return i;
    }
    return -1;
}
bool HudInput::button_at(HudRect button,Vec2 p) {
    const double x=p.x-button.x,y=p.y-button.y,w=button.width,h=button.height;
    if(x<0||x>=w||y<0||y>=h)return false;
    return !(x+y<4||w-x+y<4||w-x+h-y<4||x+h-y<4);
}
int HudInput::slot_at(Vec2 p,int selected,bool container) {
    if(potion_at(p,selected)>=0)return -1;
    const bool equipment=p.y>=304&&p.y<344,lower=container&&p.y>=container_y&&p.y<container_y+84;
    if(!equipment&&!lower&&(selected==1||p.y<380||p.y>=464))return -1;
    if(p.x<14||p.x>=186)return -1;
    const int top=equipment?304:lower?container_y:380;
    const int col=static_cast<int>((p.x-14)/44),row=equipment?0:static_cast<int>((p.y-top)/44);
    const double x=p.x-14-44*col,y=p.y-top-44*row;
    if(x>=40||y>=40)return -1;
    // Cut corners follow ItemGrid.CutsByNum at the same four-pixel radius.
    if(col==0&&((row==0&&x+y<4)||((equipment||row==1)&&x+40-y<4)))return -1;
    if(col==3&&((row==0&&40-x+y<4)||((equipment||row==1)&&80-x-y<4)))return -1;
    return equipment?col:(lower?container_slot:selected==2?12:4)+row*4+col;
}
int HudInput::tab_at(Vec2 point,bool backpack) {
    if(point.y<354||point.y>=373||point.x<7||point.x>=7+30*(backpack?3:2))return -1;
    const int tab=static_cast<int>((point.x-7)/30);
    return point.x-7-30*tab<28 ? tab : -1;
}
void HudInput::down(Vec2 point,const HudState::Inventory& state) {
    pointer_=start_=point;down_slot_=slot_at(point,selected_,state.container>=0);dragging_=false;
    down_tab_=tab_at(point,state.backpack);
    if(down_slot_>=0&&state.items.at(static_cast<std::size_t>(down_slot_))==-1)down_slot_=-1;
}
void HudInput::move(Vec2 point,int selected,bool container) {
    pointer_=point;
    const int slot=slot_at(point,selected,container);
    if(slot!=click_slot_)click_slot_=-1;
    // PotionSlotView MOUSE_OUT cancels its pending second click.
    if(click_potion_>=0&&potion_at(point,selected)!=click_potion_)click_potion_=-1;
    if(down_slot_<0||dragging_)return;
    // Original MOUSE_OUT cancels the drag check before a drag has started.
    if(slot!=down_slot_) { down_slot_=-1;return; }
    if(std::hypot(point.x-start_.x,point.y-start_.y)>3) { dragging_=true;click_slot_=-1; }
}
std::optional<InventoryAction> HudInput::up(Vec2 point,bool shift,bool control,std::int32_t time,const HudState::Inventory& state) {
    pointer_=point;const int source=down_slot_,target=slot_at(point,selected_,state.container>=0);down_slot_=-1;
    const int tab=down_tab_;down_tab_=-1;
    if(dragging_) {
        dragging_=false;click_slot_=-1;
        if(target==source)return {};
        if(target>=0&&target!=source)return InventoryAction{InventoryActionKind::swap,source,target};
        if(point.x<0)return InventoryAction{InventoryActionKind::drop,source};
        if(point.x>=7&&point.x<193&&point.y>=354&&point.y<499)return InventoryAction{InventoryActionKind::store,source};
        return {};
    }
    if(tab>=0&&tab==tab_at(point,state.backpack)) { cancel();selected_=tab;return {}; }
    // PotionSlotView acts on MOUSE_UP alone, wherever the press began.
    if(const int potion=potion_at(point,selected_);potion>=0) {
        const auto elapsed=static_cast<std::int64_t>(time)-potion_time_;
        if(!shift&&(click_potion_!=potion||elapsed<0||elapsed>=250)) { click_potion_=potion;potion_time_=time;return {}; }
        click_potion_=-1;return InventoryAction{InventoryActionKind::potion,potion};
    }
    if(source<0||target!=source)return {};
    if(shift) { click_slot_=-1;if(source>=4)return InventoryAction{InventoryActionKind::use,source};return {}; }
    // ItemGridMediator.onCtrlClick only handles InventoryGrid tiles.
    if(control) { click_slot_=-1;if(source>=4&&source<container_slot)return InventoryAction{InventoryActionKind::transfer,source};return {}; }
    if(click_slot_==source&&static_cast<std::int64_t>(time)-click_time_>=0&&static_cast<std::int64_t>(time)-click_time_<250) {
        click_slot_=-1;return InventoryAction{InventoryActionKind::equip,source};
    }
    click_slot_=source;click_time_=time;return {};
}
void HudInput::cancel(bool reset_tabs) { down_slot_=click_slot_=down_tab_=click_potion_=-1;dragging_=false;pressed_button_=-1;if(reset_tabs)selected_=0; }
void HudInput::container_changed() {
    if(down_slot_>=container_slot) { down_slot_=-1;dragging_=false; }
    if(click_slot_>=container_slot)click_slot_=-1;
}
void HudInput::cycle_tabs(bool backpack) { cancel();selected_=(selected_+1)%(backpack?3:2); }
}
