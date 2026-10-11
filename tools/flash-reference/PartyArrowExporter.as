package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.objects.ObjectLibrary;
import com.company.assembleegameclient.objects.Player;
import com.company.assembleegameclient.map.Camera;
import com.company.assembleegameclient.map.partyoverlay.PlayerArrow;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.sound.SoundEffectLibrary;
import kabam.rotmg.core.StaticInjectorContext;
import kabam.rotmg.assets.services.CharacterFactory;
import kabam.rotmg.game.signals.AddTextLineSignal;
import org.swiftsuspenders.Injector;
import flash.display.Sprite;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.geom.Matrix;
import flash.geom.Rectangle;
import flash.events.InvokeEvent;
import flash.events.MouseEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.media.Sound;
public class PartyArrowExporter extends Sprite {
 public function PartyArrowExporter() { Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
 private function run(event:InvokeEvent):void {
  try {
   StaticInjectorContext.injector=new Injector();StaticInjectorContext.injector.map(AddTextLineSignal).toValue(new AddTextLineSignal());StaticInjectorContext.injector.map(CharacterFactory).toValue(new CharacterFactory());
   Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;new ReferenceAssets().load();
   for each(var xml:XML in ObjectLibrary.xmlLibrary_) { SoundEffectLibrary.nameMap_[String(xml.HitSound)||"monster/default_hit"]=new Sound();SoundEffectLibrary.nameMap_[String(xml.DeathSound)||"monster/default_death"]=new Sound(); }
   var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();var output:File=new File(tasks.output);output.createDirectory();var metrics:Array=[];
   for each(var task:Object in tasks.cases) {
    var z:Number=task.zoom,w:Number=task.width,h:Number=task.height;
    var camera:Camera=new Camera();var rect:Rectangle=new Rectangle(-(w-h/3)/(2*z),-h/z*(task.centered?13/24:3/4),(w-h/3)/z,h/z);camera.configure(50,50,12,task.angle,rect,false);
    var holder:Sprite=new Sprite();addChild(holder);holder.x=-rect.x*z;holder.y=-rect.y*z;holder.scaleX=holder.scaleY=z;
    var arrow:PlayerArrow=new PlayerArrow();holder.addChild(arrow);var players:Array=[];
    for each(var row:Object in task.players) { var p:Player=ObjectLibrary.getObjectFromType(row.type) as Player;p.name_=row.name;p.level_=row.level;p.x_=task.x;p.y_=task.y;players.push(p); }
    arrow.setGameObject(players[0]);for(var i:int=1;i<players.length;i++)arrow.addGameObject(players[i]);arrow.draw(0,camera);
    if(task.hover) { arrow.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER));if(!task.tooltip)arrow.removeChildAt(1);else { arrow.getChildAt(1).scaleX=arrow.getChildAt(1).scaleY=h/600; }arrow.draw(0,camera); }
    metrics.push({id:task.id,x:holder.x+arrow.x*z,y:holder.y+arrow.y*z,rotation:arrow.getChildAt(0).rotation});
    var image:BitmapData=new BitmapData(w,h,true,0);image.draw(holder,new Matrix(z,0,0,z,holder.x,holder.y));
    stream.open(output.resolvePath(task.id+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose();removeChild(holder);
   }
   stream.open(output.resolvePath("metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics));stream.close();NativeApplication.nativeApplication.exit(0);
  } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); }
 }
}
}
