package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.objects.ObjectLibrary;
import com.company.assembleegameclient.objects.Player;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.ui.tooltip.PlayerToolTip;
import com.company.assembleegameclient.ui.RankText;
import com.company.assembleegameclient.ui.GuildText;
import com.company.assembleegameclient.ui.StatusBar;
import com.company.assembleegameclient.sound.SoundEffectLibrary;
import kabam.rotmg.core.StaticInjectorContext;
import kabam.rotmg.assets.services.CharacterFactory;
import kabam.rotmg.game.signals.AddTextLineSignal;
import org.swiftsuspenders.Injector;
import flash.display.Sprite;
import flash.display.DisplayObject;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.geom.Matrix;
import flash.geom.Rectangle;
import flash.text.TextField;
import flash.filters.DropShadowFilter;
import com.company.util.GraphicsUtil;
import flash.display.GraphicsPath;
import flash.events.InvokeEvent;
import flash.events.MouseEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.media.Sound;
import com.company.ui.SimpleText;
import com.company.assembleegameclient.ui.menu.MenuOption;
import com.company.assembleegameclient.ui.GameObjectListItem;
import com.company.util.AssetLibrary;
import flash.geom.ColorTransform;
import com.company.assembleegameclient.ui.TextBoxLine;
import com.company.assembleegameclient.util.FameUtil;
import flash.text.engine.TextBlock;
import flash.text.engine.TextLine;
import flash.filters.GlowFilter;
import flash.text.TextFormat;
import flash.text.TextFieldType;
import com.company.assembleegameclient.map.mapoverlay.SpeechBalloon;
import com.company.assembleegameclient.objects.GameObject;
import com.company.assembleegameclient.ui.TradeInventory;
import com.company.assembleegameclient.ui.IconButton;
import com.company.assembleegameclient.ui.TradeButton;
import com.company.assembleegameclient.ui.TradeSlot;
import com.company.assembleegameclient.ui.TextButton;
import com.company.assembleegameclient.ui.panels.TradeRequestPanel;
import kabam.rotmg.messaging.impl.data.TradeItem;
public class PlayerTooltipExporter extends Sprite {
    private var output:File;
    public function PlayerTooltipExporter() { Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
    private function save(key:String,object:DisplayObject,w:int,h:int,dx:Number=16,dy:Number=16):void {
        var image:BitmapData=new BitmapData(w,h,true,0);image.draw(object,new Matrix(1,0,0,1,dx,dy));
        var stream:FileStream=new FileStream();stream.open(output.resolvePath(key+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose();
    }
    private function describe(object:DisplayObject):Object {
        var bounds:Rectangle=object.getBounds(object);
        var info:Object={x:object.x,y:object.y,width:object.width,height:object.height,bounds:{x:bounds.x,y:bounds.y,width:bounds.width,height:bounds.height}};
        if(object is TextField) { var field:TextField=object as TextField;info.text=field.text;info.textWidth=field.textWidth;info.textHeight=field.textHeight; }
        if(object is Sprite) { info.children=[];for(var i:int=0;i<(object as Sprite).numChildren;++i)info.children.push(describe((object as Sprite).getChildAt(i))); }
        return info;
    }
    private function run(event:InvokeEvent):void {
        try {
            StaticInjectorContext.injector=new Injector();StaticInjectorContext.injector.map(AddTextLineSignal).toValue(new AddTextLineSignal());StaticInjectorContext.injector.map(CharacterFactory).toValue(new CharacterFactory());
            Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;new ReferenceAssets().load();
            for each(var xml:XML in ObjectLibrary.xmlLibrary_) { SoundEffectLibrary.nameMap_[String(xml.HitSound)||"monster/default_hit"]=new Sound();SoundEffectLibrary.nameMap_[String(xml.DeathSound)||"monster/default_death"]=new Sound(); }
            var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();output=new File(tasks.output);output.createDirectory();
            var metrics:Array=[];
            if(tasks.glyphProbe) {
                for each(var sample:String in ["8","80","800/800","888/888","8 ","80/80"]) {
                    var probeField:SimpleText=new SimpleText(14,16777215);probeField.setBold(true);probeField.text=sample;probeField.updateMetrics();
                    save("glyph-auto-"+sample.replace(/\//g,"-"),probeField,100,40,0,0);probeField.width+=8;
                    save("glyph-"+sample.replace(/\//g,"-"),probeField,100,40,0,0);
                }
            } else if(tasks.bake) {
                var entries:Array=[];
                for(var rank:int=-1;rank<=99;++rank) { var badge:RankText=new RankText(rank,false,true);save("playerrank"+rank,badge,128,32,0,0);entries.push("playerrank"+rank);var badgeBackground:Sprite=new Sprite();badgeBackground.graphics.beginFill(3552822,.5);badgeBackground.graphics.drawRect(0,0,128,32);badgeBackground.graphics.endFill();badgeBackground.addChild(badge);save("playerbadge"+rank,badgeBackground,128,32,0,0);entries.push("playerbadge"+rank); }
                for each(var guildRank:int in [0,10,20,30,40]) { var guild:GuildText=new GuildText("",guildRank,136);guild.draw("X",guildRank);guild.getChildAt(1).visible=false;save("playerguild"+guildRank,guild,48,48,8,8);entries.push("playerguild"+guildRank); }
                var dummy:Player=ObjectLibrary.getObjectFromType(0x030e) as Player;var tooltip:PlayerToolTip=new PlayerToolTip(dummy);var hint:DisplayObject=tooltip.getChildAt(tooltip.numChildren-1);save("playerhint",hint,int(hint.width)+16,int(hint.height)+16,8,8);entries.push("playerhint");
                var frameShape:Sprite=new Sprite();var path:GraphicsPath=new GraphicsPath();GraphicsUtil.drawCutEdgeRect(0,0,48,48,4,[1,1,1,1],path);
                frameShape.graphics.lineStyle(1,16777215,1,false,"normal","none","round");frameShape.graphics.beginFill(3552822,.5);frameShape.graphics.drawPath(path.commands,path.data);frameShape.graphics.endFill();save("playerframe",frameShape,80,80);entries.push("playerframe");
                // PlayerMenu: MenuOption icons as TextureRedrawer produces them, in the
                // normal, mouseOverCT and TeleportMenuOption.inactiveCT states.
                for each(var icon:Array in FixtureMenu.ICONS) for each(var state:String in ["","hover","inactive"]) {
                    var iconOption:MenuOption=new MenuOption(AssetLibrary.getImageFromSet(icon[0],icon[1]),16777215,"X");
                    if(state=="hover")iconOption.setColorTransform(FixtureMenu.HOVER);else if(state=="inactive")iconOption.setColorTransform(FixtureMenu.INACTIVE);
                    var iconBitmap:DisplayObject=iconOption.getChildAt(0);iconOption.removeChild(iconBitmap);iconBitmap.x=0;iconBitmap.y=0;
                    var iconHolder:Sprite=new Sprite();iconHolder.addChild(iconBitmap);
                    save("menuicon-"+icon[0]+"-"+icon[1]+state,iconHolder,64,64,8,8);entries.push("menuicon-"+icon[0]+"-"+icon[1]+state);
                }
                // CharacterDetailsView: the Options (frame 5) and Nexus (frame 6) IconButtons, with the mouseOverCT hover state.
                for each(var headerIcon:Array in [["options",5],["nexus",6]]) for each(var headerState:String in ["","hover"]) {
                    var headerButton:IconButton=new IconButton(AssetLibrary.getImageFromSet("lofiInterfaceBig",headerIcon[1]),"X","options");
                    if(headerState=="hover")headerButton.setColorTransform(new ColorTransform(1,220/255,133/255));
                    var headerBitmap:DisplayObject=headerButton.getChildAt(0);
                    trace("ICONBUTTON "+headerIcon[0]+" "+headerBitmap.width+"x"+headerBitmap.height+" at "+headerBitmap.x+","+headerBitmap.y);
                    var headerHolder:Sprite=new Sprite();headerHolder.addChild(headerButton);headerButton.x=12;headerButton.y=12;
                    save("iconbutton-"+headerIcon[0]+headerState,headerHolder,int(headerBitmap.width),int(headerBitmap.height),0,0);entries.push("iconbutton-"+headerIcon[0]+headerState);
                }
                // TextBoxLine rank icons: FameUtil.numStarsToIcon with its own shadow.
                for(var chatStars:int=0;chatStars<=99;++chatStars) { var chatStar:Sprite=new Sprite();chatStar.addChild(FameUtil.numStarsToIcon(chatStars));save("chatstar"+chatStars,chatStar,32,32,8,8);entries.push("chatstar"+chatStars); }
                // TradeSlot backgrounds: filled slots (item hidden) in both offer
                // states, and empty slots with their slot-type art or hotkey number.
                var tradeCuts:Array=[[1,0,0,1],[0,0,0,0],[0,0,0,0],[0,1,1,0],[1,0,0,0],[0,0,0,0],[0,0,0,0],[0,1,0,0],[0,0,0,1],[0,0,0,0],[0,0,0,0],[0,0,1,0]];
                for(var tradeIndex:int=0;tradeIndex<12;++tradeIndex) {
                    for each(var offered:Boolean in [false,true]) {
                        var filled:TradeSlot=new TradeSlot(2594,true,offered,0,tradeIndex-3,tradeCuts[tradeIndex],tradeIndex);filled.itemBitmap_.visible=false;
                        var filledHolder:Sprite=new Sprite();filledHolder.addChild(filled);
                        save("tradefill"+tradeIndex+(offered ? "on" : ""),filledHolder,48,48,4,4);entries.push("tradefill"+tradeIndex+(offered ? "on" : ""));
                    }
                    for(var tradeType:int=0;tradeType<=(tradeIndex<4 ? 30 : 0);++tradeType) {
                        var empty:TradeSlot=new TradeSlot(-1,true,false,tradeType,tradeIndex-3,tradeCuts[tradeIndex],tradeIndex);
                        var emptyHolder:Sprite=new Sprite();emptyHolder.addChild(empty);
                        save("tradeempty"+tradeIndex+"t"+tradeType,emptyHolder,48,48,4,4);entries.push("tradeempty"+tradeIndex+"t"+tradeType);
                    }
                }
                var menuShape:Sprite=new Sprite();var menuPath:GraphicsPath=new GraphicsPath();GraphicsUtil.drawCutEdgeRect(0,0,48,48,4,[1,1,1,1],menuPath);
                menuShape.graphics.lineStyle(1,16777215,1,false,"normal","none","round");menuShape.graphics.beginFill(3552822,1);menuShape.graphics.drawPath(menuPath.commands,menuPath.data);menuShape.graphics.endFill();save("menuframe",menuShape,80,80);entries.push("menuframe");
                stream.open(output.resolvePath("entries.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(entries));stream.close();
            } else if(tasks.trade) {
                new SimpleText(14,16777215);
                // TradePanel's composition without GameSprite: both TradeInventory
                // grids, the Cancel TextButton and TradeButton, on the HUD background.
                for each(var tradeCase:Object in tasks.trade) {
                    var tradePanel:Sprite=new Sprite();tradePanel.graphics.beginFill(3552822,1);tradePanel.graphics.drawRect(0,0,200,400);tradePanel.graphics.endFill();
                    var sides:Array=[];
                    for(var side:int=0;side<2;++side) {
                        var tradeItems:Vector.<TradeItem>=new Vector.<TradeItem>();
                        for each(var tradeSlot:Object in tradeCase.sides[side].items) { var tradeItem:TradeItem=new TradeItem();tradeItem.item_=tradeSlot[0]==""?-1:ObjectLibrary.idToType_[tradeSlot[0]];tradeItem.slotType_=tradeSlot[1];tradeItem.tradeable_=tradeSlot[2];tradeItem.included_=tradeSlot[3];tradeItems.push(tradeItem); }
                        var inventory:TradeInventory=new TradeInventory(null,tradeCase.sides[side].name,tradeItems,side==0);inventory.x=14;inventory.y=side*174;
                        inventory.setMessage(tradeCase.sides[side].message);tradePanel.addChild(inventory);
                    }
                    var cancel:TextButton=new TextButton(16,"Cancel",80);cancel.x=50-cancel.width/2;cancel.y=400-cancel.height-10;tradePanel.addChild(cancel);
                    var accept:TradeButton=new TradeButton(16,80);accept.x=150-accept.width/2;accept.y=cancel.y;tradePanel.addChild(accept);
                    if(tradeCase.button=="disabled")accept.disable();else if(tradeCase.button=="countdown")accept.reset();else if(tradeCase.button=="waiting")accept.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
                    metrics.push({id:tradeCase.id,cancel:{x:cancel.x,y:cancel.y,w:cancel.width,h:cancel.height},accept:{x:accept.x,y:accept.y,w:accept.width,h:accept.height}});
                    save(tradeCase.id,tradePanel,200,400,0,0);
                }
                var requestHolder:Sprite=new Sprite();requestHolder.graphics.beginFill(3552822,1);requestHolder.graphics.drawRect(0,0,200,100);requestHolder.graphics.endFill();
                var requestPanel:TradeRequestPanel=new TradeRequestPanel(null,"Friend");requestPanel.x=6;requestPanel.y=8;requestHolder.addChild(requestPanel);
                save("request",requestHolder,200,100,0,0);
                var longRequest:Sprite=new Sprite();longRequest.graphics.beginFill(3552822,1);longRequest.graphics.drawRect(0,0,200,100);longRequest.graphics.endFill();
                var longPanel:TradeRequestPanel=new TradeRequestPanel(null,"Abcdefghij");longPanel.x=6;longPanel.y=8;longRequest.addChild(longPanel);
                save("request-long",longRequest,200,100,0,0);
            } else if(tasks.balloons) {
                new SimpleText(14,16777215);
                for each(var balloonCase:Object in tasks.balloons) {
                    var speaker:GameObject=ObjectLibrary.getObjectFromType(balloonCase.type);
                    var balloon:SpeechBalloon=new SpeechBalloon(speaker,balloonCase.text,balloonCase.colors[0],1,balloonCase.colors[1],1,balloonCase.colors[2],5,false,true);
                    balloon.visible=true;
                    var holder:Sprite=new Sprite();holder.addChild(balloon);
                    metrics.push({id:balloonCase.id,offsetX:balloon.offset_.x,offsetY:balloon.offset_.y,width:balloon.width,height:balloon.height,textWidth:balloon.text_.textWidth,textHeight:balloon.text_.textHeight});
                    save(balloonCase.id,holder,240,160,40,40);
                }
            } else if(tasks.chat) {
                new SimpleText(14,16777215);
                // TextBox.refreshStatusBox/addTextBlock/placeTextField for fixed lines.
                for each(var chatCase:Object in tasks.chat) {
                    var box:Sprite=new Sprite();var textSprite:Sprite=new Sprite();textSprite.x=2;textSprite.filters=[new GlowFilter(0,1,3,3,2,1)];box.addChild(textSprite);
                    var textY:Number=0;var lineInfo:Array=[];
                    for each(var chatLine:Object in chatCase.lines) {
                        var block:TextBlock=new TextBoxLine(0,chatLine.name,-1,chatLine.stars,chatLine.recipient,chatLine.toMe,chatLine.text).getTextBlock();
                        var blockSprite:Sprite=new Sprite();var textLine:TextLine=null;var lineY:Number=0;
                        while(textLine=block.createTextLine(textLine,lineY==0 ? 596 : 576)) {
                            textLine.x=lineY==0 ? 0 : 20;textLine.y=textLine.ascent+lineY;
                            lineInfo.push({ascent:textLine.ascent,descent:textLine.descent,height:textLine.height,width:textLine.width,chars:textLine.rawTextLength});
                            lineY+=Math.max(16,textLine.height);blockSprite.addChild(textLine);
                        }
                        if(textY!=0)textY+=4;
                        blockSprite.y=textY;textSprite.addChild(blockSprite);textY+=lineY;
                    }
                    if(chatCase.input!=null) {
                        var format:TextFormat=new TextFormat();format.font="Myriad Pro";format.size=14;format.color=16777215;format.bold=true;
                        var field:TextField=new TextField();field.embedFonts=true;field.defaultTextFormat=format;field.type=TextFieldType.INPUT;field.border=true;field.borderColor=16777215;
                        field.maxChars=128;field.filters=[new GlowFilter(0,1,3,3,2,1)];field.width=598;field.height=18;field.y=580;field.text=chatCase.input;
                        field.setSelection(field.length,field.length);field.scrollH=field.maxScrollH;box.addChild(field);
                        textSprite.y=600-8-18-textY;
                    } else textSprite.y=600-4-textY;
                    metrics.push({id:chatCase.id,height:textY,lines:lineInfo});
                    save(chatCase.id,box,600,600,0,0);
                }
            } else if(tasks.menus) {
                for each(var menuCase:Object in tasks.menus) {
                    var target:Player=ObjectLibrary.getObjectFromType(menuCase.type) as Player;target.name_=menuCase.name;target.level_=menuCase.level;target.isFellowGuild_=menuCase.fellowGuild;
                    var menu:FixtureMenu=new FixtureMenu(target,menuCase.options,menuCase.hover,menuCase.cooldown);
                    metrics.push({id:menuCase.id,width:menu.width,height:menu.height,tree:describe(menu)});
                    save(menuCase.id,menu,400,300,24,24);
                }
            } else for each(var data:Object in tasks.cases) {
                var p:Player=ObjectLibrary.getObjectFromType(data.type) as Player;p.name_=data.name;p.level_=data.level;p.numStars_=data.stars;p.guildName_=data.guild;p.guildRank_=data.guildRank;p.isFellowGuild_=data.fellowGuild;
                p.hp_=data.hp;p.maxHP_=data.maxHp;p.maxHPBoost_=data.hpBoost;p.maxHPMax_=data.hpCap;p.mp_=data.mp;p.maxMP_=data.maxMp;p.maxMPBoost_=data.mpBoost;p.maxMPMax_=data.mpCap;
                for(var slot:int=0;slot<4;++slot) { p.equipment_[slot]=data.items[slot];p.itemDatas_[slot]=data.datas[slot]; }
                Parameters.data_.showTierTag=true;
                tooltip=new PlayerToolTip(p);tooltip.draw();
                var info:Object={id:data.id,contentWidth:tooltip.contentWidth_,contentHeight:tooltip.contentHeight_,tree:describe(tooltip)};metrics.push(info);
                save(data.id,tooltip,512,380);
                tooltip.filters=[];save(data.id+"-unfiltered",tooltip,512,380);
                for(slot=0;slot<tooltip.numChildren;++slot)save(data.id+"-child"+slot,tooltip.getChildAt(slot),512,380);
                var hpBar:StatusBar=tooltip.getChildAt(data.guild.length ? 3 : 2) as StatusBar;save(data.id+"-hp-value",hpBar.valueText_,100,40,8,8);
            }
            stream.open(output.resolvePath("layout.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics));stream.close();NativeApplication.nativeApplication.exit(0);
        } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); }
    }
}
}

import com.company.assembleegameclient.ui.menu.Menu;
import com.company.assembleegameclient.ui.menu.MenuOption;
import com.company.assembleegameclient.ui.GameObjectListItem;
import com.company.assembleegameclient.objects.Player;
import com.company.util.AssetLibrary;
import com.company.ui.SimpleText;
import flash.geom.ColorTransform;
// PlayerMenu without GameSprite: the same header, options and cut-edge frame.
class FixtureMenu extends Menu {
    public static const ICONS:Array=[["lofiInterface2",3],["lofiInterfaceBig",10],["lofiInterface2",5],["lofiInterface2",6],["lofiInterfaceBig",7],["lofiInterfaceBig",8],["lofiInterfaceBig",9]];
    public static const LABELS:Array=["Teleport","Invite","Lock","Unlock","Trade","Ignore","Unignore"];
    public static const HOVER:ColorTransform=new ColorTransform(1,220/255,133/255);
    public static const INACTIVE:ColorTransform=new ColorTransform(84/255,84/255,84/255);
    public function FixtureMenu(player:Player,options:Array,hover:int,cooldown:Number) {
        super(3552822,16777215);
        addChild(new GameObjectListItem(11776947,true,player));
        for(var i:int=0;i<options.length;++i) {
            var kind:int=options[i];var option:MenuOption=new MenuOption(AssetLibrary.getImageFromSet(ICONS[kind][0],ICONS[kind][1]),16777215,LABELS[kind]);
            if(kind==0&&cooldown>0) {
                var bar:SimpleText=new SimpleText(18,16777215,false,0,0);bar.setBold(true);bar.text="Teleport";bar.updateMetrics();bar.x=20;bar.y=-6;
                bar.width=bar.width*(1-cooldown/10000);option.addChild(bar);option.setColorTransform(INACTIVE);
            } else if(i==hover)option.setColorTransform(HOVER);
            addOption(option);
        }
        draw();
    }
}
