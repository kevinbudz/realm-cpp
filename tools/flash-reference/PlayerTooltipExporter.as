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
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.media.Sound;
import com.company.ui.SimpleText;
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
                stream.open(output.resolvePath("entries.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(entries));stream.close();
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
