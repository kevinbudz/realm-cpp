package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.objects.ObjectLibrary;
import com.company.assembleegameclient.objects.GameObject;
import com.company.assembleegameclient.objects.Player;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.ui.panels.itemgrids.EquippedGrid;
import com.company.assembleegameclient.ui.StatusBar;
import com.company.assembleegameclient.ui.panels.itemgrids.InventoryGrid;
import com.company.assembleegameclient.ui.panels.itemgrids.ContainerGrid;
import com.company.assembleegameclient.ui.panels.InteractPanel;
import com.company.assembleegameclient.ui.panels.PortalPanel;
import com.company.assembleegameclient.ui.panels.PartyPanel;
import com.company.assembleegameclient.ui.panels.Panel;
import com.company.assembleegameclient.ui.panels.GuildBoardPanel;
import com.company.assembleegameclient.ui.panels.GuildChroniclePanel;
import com.company.assembleegameclient.ui.panels.CharacterChangerPanel;
import kabam.rotmg.characters.reskin.view.ReskinPanel;
import kabam.rotmg.game.view.SellableObjectPanel;
import kabam.rotmg.util.components.LegacyBuyButton;
import kabam.lib.net.api.MessageProvider;
import kabam.lib.net.impl.MessageCenter;
import robotlegs.bender.framework.api.ILogger;
import robotlegs.bender.framework.impl.Logger;
import kabam.rotmg.core.model.PlayerModel;
import kabam.rotmg.game.model.GameModel;
import kabam.rotmg.classes.model.ClassesModel;
import kabam.lib.net.impl.SocketServer;
import kabam.rotmg.game.model.PotionInventoryModel;
import kabam.rotmg.ui.model.TabStripModel;
import kabam.rotmg.core.view.Layers;
import com.company.assembleegameclient.game.GameSprite;
import com.company.assembleegameclient.objects.SellableObject;
import com.company.assembleegameclient.objects.Merchant;
import kabam.rotmg.messaging.impl.OutstandingBuy;
import com.company.util.MoreColorUtil;
import flash.geom.ColorTransform;
import com.company.assembleegameclient.ui.TextButton;
import com.company.assembleegameclient.objects.Portal;
import flash.events.MouseEvent;
import flash.text.TextField;
import flash.text.TextLineMetrics;
import com.company.assembleegameclient.ui.panels.itemgrids.itemtiles.ItemTile;
import com.company.assembleegameclient.ui.panels.itemgrids.itemtiles.EquipmentTile;
import com.company.assembleegameclient.ui.panels.itemgrids.itemtiles.InventoryTile;
import com.company.assembleegameclient.ui.panels.itemgrids.itemtiles.ItemTileSprite;
import com.company.util.GraphicsUtil;
import com.company.assembleegameclient.sound.SoundEffectLibrary;
import kabam.rotmg.game.view.components.TabStripView;
import kabam.rotmg.game.view.components.StatsView;
import kabam.rotmg.assets.services.IconFactory;
import kabam.rotmg.core.StaticInjectorContext;
import kabam.rotmg.assets.services.CharacterFactory;
import kabam.rotmg.game.signals.AddTextLineSignal;
import kabam.rotmg.ui.view.PotionInventoryView;
import kabam.rotmg.ui.view.components.PotionSlotView;
import org.swiftsuspenders.Injector;
import flash.display.Sprite;
import flash.display.DisplayObject;
import flash.display.Bitmap;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.display.GraphicsPath;
import flash.geom.Matrix;
import flash.geom.Point;
import flash.media.Sound;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
public class InventoryExporter extends Sprite {
    private var output:File;
    private var entries:Array=[];
    private var metrics:Array=[];
    private var potionLayout:Array=[];
    private var containerLayout:Array=[];
    private var portalLayout:Array=[];
    private var buttonLayout:Array=[];
    private var sellableLayout:Array=[];
    private const POTIONS:Array=[2594,2595];
    private const EQUIP_CUTS:Array=[[1,0,0,1],[0,0,0,0],[0,0,0,0],[0,1,1,0]];
    private const INV_CUTS:Array=[[1,0,0,0],[0,0,0,0],[0,0,0,0],[0,1,0,0],[0,0,0,1],[0,0,0,0],[0,0,0,0],[0,0,1,0]];
    public function InventoryExporter() {
        Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,invoked);
    }
    private function save(key:String,object:DisplayObject,w:int,h:int,dx:Number=0,dy:Number=0):void {
        var bd:BitmapData=new BitmapData(w,h,true,0);bd.draw(object,new Matrix(1,0,0,1,dx,dy));
        var stream:FileStream=new FileStream();stream.open(output.resolvePath(key+".png"),FileMode.WRITE);stream.writeBytes(bd.encode(bd.rect,new PNGEncoderOptions()));stream.close();bd.dispose();entries.push(key);
    }
    private function backdrop(backpack:Boolean,selected:int,stats:StatsView=null):Sprite {
        var result:Sprite=new Sprite();result.graphics.beginFill(3552822);result.graphics.drawRect(0,300,200,200);result.graphics.endFill();
        var path:GraphicsPath=new GraphicsPath();GraphicsUtil.drawCutEdgeRect(11,301,178,46,6,[1,1,1,1],path);
        result.graphics.beginFill(6776679);result.graphics.drawPath(path.commands,path.data);result.graphics.endFill();
        var strip:TabStripView=new TabStripView(186,153);strip.x=7;strip.y=346;
        var factory:IconFactory=new IconFactory();
        for(var i:int=0;i<(backpack?3:2);++i)strip.addTab(factory.makeIconBitmap(24+i),i==1&&stats ? stats : new Sprite());
        strip.setSelectedTab(selected);result.addChild(strip);return result;
    }
    // A PortalPanel on stage, so ADDED_TO_STAGE positions its children.
    private function portalPanel(type:int,name:String,active:Boolean,p:Player):PortalPanel {
        var portal:Portal=ObjectLibrary.getObjectFromType(type) as Portal;portal.name_=name;portal.active_=active;
        var interact:InteractPanel=new InteractPanel(null,p,200,100);var panel:PortalPanel=new PortalPanel(null,portal);interact.setPanel(panel);
        stage.addChild(interact);panel.draw();return panel;
    }
    // GuildBoard, GuildChronicle, CharacterChanger and ReskinVendor getPanel, in HudState::Button kind order.
    private function buttonPanel(kind:int,p:Player):Panel {
        var panel:Panel=kind==0 ? new GuildBoardPanel(null) : kind==1 ? new GuildChroniclePanel(null) : kind==2 ? new CharacterChangerPanel(null) : new ReskinPanel(null);
        var interact:InteractPanel=new InteractPanel(null,p,200,100);interact.setPanel(panel);stage.addChild(interact);return panel;
    }
    // SellableObjectPanel.draw reads gs_.map.player_ and gs_.gsc_.outstandingBuy_.
    private var game:GameSprite=null;
    private function gameSprite(p:Player):GameSprite {
        if(game==null) {
            StaticInjectorContext.injector.map(ILogger).toValue(new Logger(this,null));StaticInjectorContext.injector.map(MessageProvider).toValue(new MessageCenter());
            // Direct instances: their own [Inject] properties are not needed by the panel.
            StaticInjectorContext.injector.map(PlayerModel).toValue(new PlayerModel());StaticInjectorContext.injector.map(GameModel).toValue(new GameModel());StaticInjectorContext.injector.map(ClassesModel).toValue(new ClassesModel());StaticInjectorContext.injector.map(SocketServer).toValue(new SocketServer());StaticInjectorContext.injector.map(PotionInventoryModel).toValue(new PotionInventoryModel());StaticInjectorContext.injector.map(TabStripModel).toValue(new TabStripModel());StaticInjectorContext.injector.map(Layers).toValue(new Layers());
            game=new GameSprite(0,false,0,null,null);
        }
        game.map.player_=p;return game;
    }
    // SellableObject.getPanel inside an InteractPanel at HUD y 500, then InteractPanel's per-frame draw.
    private function sellablePanel(data:Object,p:Player):SellableObjectPanel {
        var owner:SellableObject=ObjectLibrary.getObjectFromType(data.type) as SellableObject;
        if(owner is Merchant)(owner as Merchant).setMerchandiseType(data.merchandise);
        if(data.hasOwnProperty("price"))owner.setPrice(data.price);
        if(data.hasOwnProperty("currency"))owner.setCurrency(data.currency);
        if(data.hasOwnProperty("rankReq"))owner.setRankReq(data.rankReq);
        p.numStars_=data.stars;p.guildRank_=data.guildRank;
        var gs:GameSprite=gameSprite(p);gs.gsc_.outstandingBuy_=data.outstanding ? new OutstandingBuy("",0,0) : null;
        var interact:InteractPanel=new InteractPanel(null,p,200,100);var panel:SellableObjectPanel=new SellableObjectPanel(gs,owner);interact.setPanel(panel);
        stage.addChild(interact);panel.draw();return panel;
    }
    private function hideSellableNameAndIcon(panel:SellableObjectPanel):void {
        for(var i:int=0;i<panel.numChildren;++i)if(panel.getChildAt(i) is TextField&&i==0||panel.getChildAt(i) is Bitmap)panel.getChildAt(i).visible=false;
    }
    private function hover(panel:Sprite):void {
        for(var i:int=0;i<panel.numChildren;++i)if(panel.getChildAt(i) is TextButton||panel.getChildAt(i) is LegacyBuyButton)panel.getChildAt(i).dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER));
    }
    private function player(type:int):Player { return ObjectLibrary.getObjectFromType(type) as Player; }
    private function invoked(event:InvokeEvent):void {
        try {
            StaticInjectorContext.injector=new Injector();StaticInjectorContext.injector.map(AddTextLineSignal).toValue(new AddTextLineSignal());StaticInjectorContext.injector.map(CharacterFactory).toValue(new CharacterFactory());
            Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;new ReferenceAssets().load();
            for each(var xml:XML in ObjectLibrary.xmlLibrary_) {
                SoundEffectLibrary.nameMap_[String(xml.HitSound)||"monster/default_hit"]=new Sound();
                SoundEffectLibrary.nameMap_[String(xml.DeathSound)||"monster/default_death"]=new Sound();
            }
            var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            output=new File(tasks.output);output.createDirectory();
            if(tasks.probe) {
                // Every PartyPanel ColorTransform, applied through a transformed parent Sprite as
                // GameObjectListItem does. Source x is the channel and y the alpha; over black,
                // the untransformed draw reveals the stored premultiplied channel.
                var square:BitmapData=new BitmapData(256,256,true,0);
                for(var sy:int=0;sy<256;++sy)for(var sx:int=0;sx<256;++sx)square.setPixel32(sx,sy,(uint(sy)<<24)|(sx<<16)|((255-sx)<<8)|((sx*7)&255));
                for(var dark:int=0;dark<2;++dark)for(var step:int=-1;step<10;++step) {
                    var pct:ColorTransform=null;
                    if(step>=0) { var prv:Number=step/10;pct=new ColorTransform(1,1,1,1,prv*128,-prv*128,-prv*128); }
                    if(dark) { if(pct!=null)pct.concat(MoreColorUtil.darkCT);else pct=MoreColorUtil.darkCT; }
                    for each(var backdrop0:uint in tasks.probe.backdrops) {
                        var holder:Sprite=new Sprite();holder.addChild(new Bitmap(square));if(pct)holder.transform.colorTransform=pct;
                        var wrapper:Sprite=new Sprite();wrapper.addChild(holder);
                        var target0:BitmapData=new BitmapData(256,256,false,backdrop0);target0.draw(wrapper);
                        save("alpha-d"+dark+"-s"+step+"-b"+backdrop0.toString(16),new Bitmap(target0),256,256);
                    }
                }
                NativeApplication.nativeApplication.exit(0);return;
            }
            if(tasks.bake) {
                // These six fixed labels include TextField edge clipping.
                // Column centering places them on the quarter/three-quarter
                // horizontal grid and the half-pixel vertical grid.
                var labelStats:StatsView=new StatsView(191,45);
                for(var labelIndex:int=0;labelIndex<6;++labelIndex)for(var labelPhase:int=5;labelPhase<=15;labelPhase+=10) {
                    var label:DisplayObject=labelStats.stats_[labelIndex].nameText_;
                    save("statlabel"+labelIndex+"p"+labelPhase,label,int(label.width)+16,int(label.height)+16,8+labelPhase/20,8.5);
                }
                var p:Player=player(0x030e);var grid:EquippedGrid=new EquippedGrid(p,p.slotTypes_,p);
                for(var position:int=0;position<4;++position) {
                    var tile:EquipmentTile=new EquipmentTile(position,grid,false);tile.drawBackground(EQUIP_CUTS[position]);
                    save("equipment"+position,tile,40,40);
                    var er:Sprite=new Sprite();var ep:GraphicsPath=new GraphicsPath();GraphicsUtil.drawCutEdgeRect(0,0,40,40,4,EQUIP_CUTS[position],ep);er.graphics.beginFill(6036765);er.graphics.drawPath(ep.commands,ep.data);er.graphics.endFill();save("equiprestricted"+position,er,40,40);
                    for(var slotType:int=1;slotType<=25;++slotType)if(slotType!=10) {
                        tile=new EquipmentTile(position,grid,false);tile.drawBackground(EQUIP_CUTS[position]);tile.setType(slotType);
                        save("empty"+slotType+"p"+position,tile,40,40);
                    }
                }
                var inv:InventoryGrid=new InventoryGrid(p,p,4);
                for(position=0;position<8;++position) {
                    var it:InventoryTile=new InventoryTile(4+position,inv,false);it.drawBackground(INV_CUTS[position]);save("inventory"+position,it,40,40);
                    it.addTileNumber(position+1);save("hotkey"+position,it,40,40);
                    // The same cut shape, painted in the source restricted-use color.
                    var red:Sprite=new Sprite();var path:GraphicsPath=new GraphicsPath();GraphicsUtil.drawCutEdgeRect(0,0,40,40,4,INV_CUTS[position],path);
                    red.graphics.beginFill(6036765);red.graphics.drawPath(path.commands,path.data);red.graphics.endFill();save("restricted"+position,red,40,40);
                }
                for(var back:int=0;back<2;++back)for(var selected:int=0;selected<(back?3:2);++selected)save("backdrop"+back+"s"+selected,backdrop(Boolean(back),selected),200,200,0,-300);
                // PotionSlotView children: background, count field, then icon.
                var potionViews:PotionInventoryView=new PotionInventoryView();
                for(var potion:int=0;potion<2;++potion) {
                    var pv:PotionSlotView=potionViews.getChildAt(potion) as PotionSlotView;
                    pv.setData(1,true,POTIONS[potion]);save("potionuse"+potion,pv.getChildAt(0),84,24);
                    var picon:Bitmap=pv.getChildAt(2) as Bitmap;save("potion"+POTIONS[potion],picon,picon.bitmapData.width,picon.bitmapData.height);
                    pv.setData(0,true);save("potionbuy"+potion,pv.getChildAt(0),84,24);
                }
                // Fixed PortalPanel children in interaction-panel coordinates.
                var fixed:PortalPanel=portalPanel(0x0704,"",true,p);fixed.getChildAt(0).visible=false;
                save("portalenter0",fixed,200,100,6,8);hover(fixed);save("portalenter1",fixed,200,100,6,8);
                fixed.owner_.active_=false;fixed.draw();save("portalfull",fixed,200,100,6,8);stage.removeChild(fixed.parent);
                fixed=portalPanel(0x0721,"",true,p);fixed.getChildAt(0).visible=false;save("portallocked",fixed,200,100,6,8);stage.removeChild(fixed.parent);
                // LegacyBuyButton: a 40x26 cut-edge shape per fill (normal, hover, disabled) at
                // x phase 0 and 0.5; the HUD splits it into end caps and a middle column.
                const BUY_FILLS:Array=[16777215,16768133,8355711];
                for(var fill:int=0;fill<3;++fill)for(var buyPhase:int=0;buyPhase<2;++buyPhase) {
                    var buyShape:Sprite=new Sprite();var buyPath:GraphicsPath=new GraphicsPath();GraphicsUtil.drawCutEdgeRect(0,0,40,26,4,[1,1,1,1],buyPath);
                    buyShape.graphics.beginFill(BUY_FILLS[fill]);buyShape.graphics.drawPath(buyPath.commands,buyPath.data);buyShape.graphics.endFill();
                    save("buybutton"+fill+"p"+buyPhase,buyShape,41,26,buyPhase*0.5,0);
                }
                save("currency0",new Bitmap(LegacyBuyButton.coin),20,20);save("currency1",new Bitmap(LegacyBuyButton.fame),20,20);save("currency2",new Bitmap(LegacyBuyButton.guildFame),20,20);
                // SellableObjectPanel notices in interaction-panel coordinates, without the name and icon.
                for(var rank:int=1;rank<=99;++rank) {
                    var rankPanel:SellableObjectPanel=sellablePanel({type:0x01ca,merchandise:0x0c50,price:0,currency:0,rankReq:rank,stars:0,guildRank:-1},p);
                    hideSellableNameAndIcon(rankPanel);save("sellrank"+rank,rankPanel,200,100,6,8);stage.removeChild(rankPanel.parent);
                }
                var guildPanel:SellableObjectPanel=sellablePanel({type:0x0736,stars:0,guildRank:-1},p);hideSellableNameAndIcon(guildPanel);save("sellguild",guildPanel,200,100,6,8);stage.removeChild(guildPanel.parent);
                for each(var iconType:int in [0x0505,0x0736,0x0737,0x0738]) {
                    var iconOwner:SellableObject=ObjectLibrary.getObjectFromType(iconType) as SellableObject;var iconData:BitmapData=iconOwner.getIcon();
                    save("sellicon"+iconType,new Bitmap(iconData),iconData.width,iconData.height);
                }
                // ButtonPanel/ReskinPanel TextButtons without their titles (child 0).
                for(var bk:int=0;bk<4;++bk) {
                    var fixedButton:Panel=buttonPanel(bk,p);fixedButton.getChildAt(0).visible=false;
                    save("buttonpanel"+bk+"h0",fixedButton,200,100,6,8);hover(fixedButton);save("buttonpanel"+bk+"h1",fixedButton,200,100,6,8);stage.removeChild(fixedButton.parent);
                }
                for(var type:* in ObjectLibrary.xmlLibrary_) {
                    xml=ObjectLibrary.xmlLibrary_[type];
                    if(String(xml.Class)=="Equipment") {
                        var item:ItemTileSprite=new ItemTileSprite();item.setType(int(type),-1);
                        var iw:int=2*Math.ceil(item.itemBitmap.bitmapData.width/2);var ih:int=2*Math.ceil(item.itemBitmap.bitmapData.height/2);
                        save("item"+type,item,iw,ih,iw/2,ih/2);item.setDim(true);save("dim"+type,item,iw,ih,iw/2,ih/2);
                        var tier:ItemTile=new ItemTile(0,grid);tier.setItem(int(type),-1);tier.itemSprite.visible=false;tier.toggleTierTag(true);
                        save("tier"+type,tier,40,40);
                    }
                    if(String(xml.Class)=="Player") { var go:GameObject=ObjectLibrary.getObjectFromType(int(type));save("portrait"+type,new Bitmap(go.getPortrait()),80,80); }
                }
                stream.open(output.resolvePath("entries.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(entries));stream.close();
            } else {
                for each(var task:Object in tasks.cases) {
                    p=player(task.type);p.mp_=task.mp;Parameters.data_.showTierTag=task.tiers;
                    var stats:StatsView=null;
                    if(task.stats) {
                        stats=new StatsView(191,45);stats.y=(153-27)/2-stats.height/2;
                        p.attack_=task.stats[0].value;p.attackBoost_=task.stats[0].boost;
                        p.defense_=task.stats[1].value;p.defenseBoost_=task.stats[1].boost;
                        p.speed_=task.stats[2].value;p.speedBoost_=task.stats[2].boost;
                        p.dexterity_=task.stats[3].value;p.dexterityBoost_=task.stats[3].boost;
                        p.vitality_=task.stats[4].value;p.vitalityBoost_=task.stats[4].boost;
                        p.wisdom_=task.stats[5].value;p.wisdomBoost_=task.stats[5].boost;
                        stats.draw(p);
                    }
                    var scene:Sprite=new Sprite();scene.graphics.beginFill(3552822);scene.graphics.drawRect(0,0,200,600);scene.graphics.endFill();
                    var portrait:Bitmap=new Bitmap(p.getPortrait());portrait.x=-2;portrait.y=190;scene.addChild(portrait);
                    var xp:StatusBar=new StatusBar(176,16,5931045,5526612,"Lvl 1");xp.x=12;xp.y=230;xp.draw(0,0,0,-1);scene.addChild(xp);
                    var bg:Sprite=backdrop(task.backpack,task.selected,stats);scene.addChild(bg);
                    grid=new EquippedGrid(p,p.slotTypes_,p);grid.x=14;grid.y=304;scene.addChild(grid);
                    var items:Vector.<int>=new Vector.<int>();var datas:Vector.<int>=new Vector.<int>();
                    for each(var id:int in task.items){items.push(id);datas.push(-1);}grid.setItems(items,datas);
                    if(task.selected!=1) {
                        inv=new InventoryGrid(p,p,task.selected==2?12:4,task.selected==2);inv.x=14;inv.y=380;scene.addChild(inv);
                        // TabStripMediator measures the grid before its mediator assigns items.
                        var gridHeight:Number=inv.height;inv.setItems(items,datas);
                        if(task.potions) {
                            var pinv:PotionInventoryView=new PotionInventoryView();pinv.x=inv.x;pinv.y=inv.y+gridHeight+4;
                            for(var pi:int=0;pi<2;++pi)(pinv.getChildAt(pi) as PotionSlotView).setData(task.potions[pi],true,POTIONS[pi]);
                            scene.addChild(pinv);potionLayout.push({id:task.id,y:pinv.y,gridHeight:gridHeight,second:pinv.getChildAt(1).x});
                        }
                    }
                    if(task.container) {
                        // Container.getPanel, then InteractPanel.setPanel positions the
                        // grid from its width before ItemGrid.draw assigns items.
                        var interact:InteractPanel=new InteractPanel(null,p,200,100);interact.y=500;
                        var cgrid:ContainerGrid=new ContainerGrid(ObjectLibrary.getObjectFromType(task.container.type),p);interact.setPanel(cgrid);
                        var citems:Vector.<int>=new Vector.<int>();var cdatas:Vector.<int>=new Vector.<int>();
                        for each(var cid:int in task.container.items){citems.push(cid);cdatas.push(-1);}cgrid.setItems(citems,cdatas);
                        scene.addChild(interact);containerLayout.push({id:task.id,x:cgrid.x,y:cgrid.y});
                    }
                    if(task.portal) {
                        var ppanel:PortalPanel=portalPanel(task.portal.type,task.portal.name,task.portal.active,p);
                        if(task.portal.hover)hover(ppanel);
                        var pinteract:DisplayObject=ppanel.parent;pinteract.y=500;scene.addChild(pinteract);
                        var nt:TextField=ppanel.getChildAt(0) as TextField;var lines:Array=[];
                        for(var li:int=0;li<nt.numLines;++li) { var lm:TextLineMetrics=nt.getLineMetrics(li);lines.push({text:nt.getLineText(li),x:lm.x,width:lm.width,height:lm.height}); }
                        var button:Object=null;
                        for(var bi:int=0;bi<ppanel.numChildren;++bi)if(ppanel.getChildAt(bi) is TextButton) { var tb:DisplayObject=ppanel.getChildAt(bi);button={x:tb.x+6,y:tb.y+508,width:tb.width,height:tb.height,shapeWidth:(tb as TextButton).w_}; }
                        portalLayout.push({id:task.id,y:nt.y,width:nt.width,height:nt.height,textWidth:nt.textWidth,textHeight:nt.textHeight,lines:lines,button:button});
                    }
                    if(task.button) {
                        var bpanel:Panel=buttonPanel(task.button.kind,p);if(task.button.hover)hover(bpanel);
                        var binteract:DisplayObject=bpanel.parent;binteract.y=500;scene.addChild(binteract);
                        var title:TextField=bpanel.getChildAt(0) as TextField;var blines:Array=[];
                        for(var bli:int=0;bli<title.numLines;++bli) { var blm:TextLineMetrics=title.getLineMetrics(bli);blines.push({text:title.getLineText(bli),x:blm.x,width:blm.width}); }
                        var bbutton:TextButton=bpanel.getChildAt(1) as TextButton;
                        buttonLayout.push({id:task.id,y:title.y,height:title.height,lines:blines,button:{x:bbutton.x+6,y:bbutton.y+508,width:bbutton.width,height:bbutton.height,shapeWidth:bbutton.w_}});
                    }
                    if(task.sellable) {
                        var spanel:SellableObjectPanel=sellablePanel(task.sellable,p);if(task.sellable.hover)hover(spanel);
                        var sinteract:DisplayObject=spanel.parent;sinteract.y=500;scene.addChild(sinteract);
                        var sentry:Object={id:task.id,children:[]};
                        for(var sci:int=0;sci<spanel.numChildren;++sci) {
                            var child:DisplayObject=spanel.getChildAt(sci);var info:Object={x:child.x,y:child.y,width:child.width,height:child.height,visible:child.visible,kind:"sprite"};
                            if(child is TextField) {
                                var stf:TextField=child as TextField;info.kind="text";info.textWidth=stf.textWidth;info.textHeight=stf.textHeight;info.lines=[];
                                for(var sli:int=0;sli<stf.numLines;++sli) { var slm:TextLineMetrics=stf.getLineMetrics(sli);info.lines.push({text:stf.getLineText(sli),x:slm.x,width:slm.width}); }
                            } else if(child is LegacyBuyButton) {
                                var lbb:LegacyBuyButton=child as LegacyBuyButton;info.kind="buy";info.enabled=lbb.mouseEnabled;
                                info.text={x:lbb.text.x,y:lbb.text.y,width:lbb.text.width,height:lbb.text.height,textWidth:lbb.text.textWidth,textHeight:lbb.text.textHeight,value:lbb.text.text};
                                info.icon={x:lbb.icon.x,y:lbb.icon.y,width:lbb.icon.width,height:lbb.icon.height};
                            } else if(child is Bitmap) info.kind="bitmap";
                            sentry.children.push(info);
                        }
                        sellableLayout.push(sentry);
                    }
                    if(task.party&&task.party.members) {
                        // InteractPanel.setPanel(partyPanel_), then the member loop of PartyPanel.draw
                        // with getTimer() replaced by the fixture time.
                        var partyPanel:PartyPanel=new PartyPanel(null);var partyInteract:InteractPanel=new InteractPanel(null,p,200,100);
                        partyInteract.setPanel(partyPanel);partyInteract.y=500;var partyTime:int=0;
                        for(var mi:int=0;mi<6;++mi) {
                            var md:Object=task.party.members[mi];var member:Player=null;var ct:ColorTransform=null;
                            if(md) {
                                member=player(md.type);member.name_=md.name;member.isFellowGuild_=md.fellow;member.starred_=md.starred;member.hp_=md.hp;member.maxHP_=md.maxHp;
                                if(member.hp_<member.maxHP_*0.2) {
                                    if(partyTime==0)partyTime=task.party.time;
                                    var rv:Number=int(Math.abs(Math.sin(partyTime/200))*10)/10;var rc:int=128;ct=new ColorTransform(1,1,1,1,rv*rc,-rv*rc,-rv*rc);
                                }
                                if(!member.starred_) { if(ct!=null)ct.concat(MoreColorUtil.darkCT);else ct=MoreColorUtil.darkCT; }
                            }
                            partyPanel.memberPanels_[mi].draw(member,ct);
                        }
                        scene.addChild(partyInteract);
                    }
                    save(task.id,scene,200,600);
                    if(stats) {
                        var fields:Array=[];
                        for(var si:int=0;si<6;++si) {
                            var sv:*=stats.stats_[si];var origin:Point=sv.localToGlobal(new Point());
                            fields.push({x:origin.x,y:origin.y,labelWidth:sv.nameText_.width,valueWidth:sv.valText_.width});
                        }
                        metrics.push({id:task.id,fields:fields});
                    }
                }
                stream.open(output.resolvePath("stats-metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics));stream.close();
                stream.open(output.resolvePath("potion-layout.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(potionLayout));stream.close();
                stream.open(output.resolvePath("container-layout.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(containerLayout));stream.close();
                stream.open(output.resolvePath("portal-layout.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(portalLayout));stream.close();
                stream.open(output.resolvePath("button-layout.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(buttonLayout));stream.close();
                stream.open(output.resolvePath("sellable-layout.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(sellableLayout));stream.close();
            }
            NativeApplication.nativeApplication.exit(0);
        } catch(error:Error) {
            trace(error.getStackTrace());
            try { var failure:FileStream=new FileStream();failure.open(new File(event.arguments[0]+".error.txt"),FileMode.WRITE);failure.writeUTFBytes(error.getStackTrace()||String(error));failure.close(); } catch(ignored:Error) {}
            NativeApplication.nativeApplication.exit(1);
        }
    }
}
}
