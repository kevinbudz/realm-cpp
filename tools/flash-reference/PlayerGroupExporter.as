package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.util.AnimatedChars;
import kabam.rotmg.assets.EmbeddedData;
import com.company.assembleegameclient.objects.ObjectLibrary;
import com.company.assembleegameclient.objects.Player;
import com.company.assembleegameclient.map.Map;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.ui.tooltip.PlayerGroupToolTip;
import com.company.assembleegameclient.ui.menu.PlayerGroupMenu;
import com.company.assembleegameclient.sound.SoundEffectLibrary;
import kabam.rotmg.core.StaticInjectorContext;
import kabam.rotmg.assets.services.CharacterFactory;
import kabam.rotmg.game.signals.AddTextLineSignal;
import org.swiftsuspenders.Injector;
import flash.display.Sprite;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.geom.Matrix;
import flash.events.InvokeEvent;
import flash.events.Event;
import flash.events.MouseEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.media.Sound;
public class PlayerGroupExporter extends Sprite {
 public function PlayerGroupExporter() { Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
 private function run(event:InvokeEvent):void {
  try {
   StaticInjectorContext.injector=new Injector();StaticInjectorContext.injector.map(AddTextLineSignal).toValue(new AddTextLineSignal());StaticInjectorContext.injector.map(CharacterFactory).toValue(new CharacterFactory());
   Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;new ReferenceAssets().load();
   for each(var xml:XML in ObjectLibrary.xmlLibrary_) { SoundEffectLibrary.nameMap_[String(xml.HitSound)||"monster/default_hit"]=new Sound();SoundEffectLibrary.nameMap_[String(xml.DeathSound)||"monster/default_death"]=new Sound(); }
   var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();var output:File=new File(tasks.output);output.createDirectory();var metrics:Array=[];
   for each(var task:Object in tasks.cases) {
    var players:Vector.<Player>=new Vector.<Player>();
    for each(var row:Object in task.players) { var p:Player=ObjectLibrary.getObjectFromType(row.type) as Player;if(row.skin)for each(var skinXML:XML in EmbeddedData.skinsXML.children())if(int(skinXML.@type)==row.skin)p.animatedChar_=AnimatedChars.getAnimatedChar(String(skinXML.AnimatedTexture.File),int(skinXML.AnimatedTexture.Index));p.setTex1(row.tex1||0);p.setTex2(row.tex2||0);p.name_=row.name;p.level_=row.level;p.isFellowGuild_=row.fellowGuild;players.push(p); }
    var holder:Sprite;
    if(task.menu) {
     var map:Map=new Map(null);map.allowPlayerTeleport_=task.teleport;
     var local:FixtureTeleportPlayer=new FixtureTeleportPlayer(ObjectLibrary.xmlLibrary_[0x030e]);local.wait=task.cooldown;map.player_=local;
     var menu:PlayerGroupMenu=new PlayerGroupMenu(map,players);holder=menu;addChild(menu);
     if(task.teleport) { if(task.hover)menu.teleportOption_.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER));menu.teleportOption_.dispatchEvent(new Event(Event.ENTER_FRAME)); }
    } else { holder=new PlayerGroupToolTip(players);addChild(holder); }
    // Capture at the same logical origin; position() still ran on the real class.
    var s:Number=task.scale;holder.scaleX=holder.scaleY=1;
    metrics.push({id:task.id,width:holder.width,height:holder.height});
    var image:BitmapData=new BitmapData(Math.ceil((holder.width+44)*s),Math.ceil((holder.height+44)*s),true,0);
    image.draw(holder,new Matrix(s,0,0,s,22*s,22*s));
    stream.open(output.resolvePath(task.id+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose();removeChild(holder);
   }
   stream.open(output.resolvePath("metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics));stream.close();NativeApplication.nativeApplication.exit(0);
  } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); }
 }
}
}
import com.company.assembleegameclient.objects.Player;
class FixtureTeleportPlayer extends Player {
 public var wait:int;
 public function FixtureTeleportPlayer(xml:XML) { super(xml); }
 override public function msUtilTeleport():int { return wait; }
}
