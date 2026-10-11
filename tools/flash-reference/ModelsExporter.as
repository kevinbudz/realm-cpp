package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.objects.*;
import com.company.assembleegameclient.map.Camera;
import com.company.assembleegameclient.map.Map;
import com.company.assembleegameclient.map.Square;
import com.company.assembleegameclient.sound.SoundEffectLibrary;
import kabam.rotmg.core.StaticInjectorContext;
import kabam.rotmg.assets.services.CharacterFactory;
import kabam.rotmg.game.signals.AddTextLineSignal;
import org.swiftsuspenders.Injector;
import flash.display.*;
import flash.geom.*;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.*;
import flash.media.Sound;
// Renders real GameObject/Object3D/ObjectFace3D classes (software path, the same
// culling and flat shading the Stage3D programs mirror). Each case places objects
// by XML id; an optional rotation replaces the object's XML Rotation.
public class ModelsExporter extends Sprite {
 private var stream:FileStream=new FileStream();
 private var tasksOutput:String="";
 public function ModelsExporter() { Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
 private function run(event:InvokeEvent):void { tasksOutput=event.arguments[0]+".error.txt";try {
  StaticInjectorContext.injector=new Injector();StaticInjectorContext.injector.map(AddTextLineSignal).toValue(new AddTextLineSignal());StaticInjectorContext.injector.map(CharacterFactory).toValue(new CharacterFactory());
  Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;Parameters.GPURenderFrame=false;Parameters.data_.GPURender=false;new ReferenceAssets().load();
  for each(var xml:XML in ObjectLibrary.xmlLibrary_) { SoundEffectLibrary.nameMap_[String(xml.HitSound)||"monster/default_hit"]=new Sound();SoundEffectLibrary.nameMap_[String(xml.DeathSound)||"monster/default_death"]=new Sound(); }
  stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();var out:File=new File(tasks.output);out.createDirectory();
  var report:Array=[];
  for each(var c:Object in tasks.cases) {
   var camera:Camera=new Camera();camera.configure(50,50,12,c.angle,new Rectangle(-300,-325,800,600),false);
   var objects:Array=[];var id:int=1;
   var map:Map=new Map(null);map.setProps(100,100,"Model reference",0,true,true);map.squares_.length=10000;
   for each(var w:Object in c.walls) {
    var wall:GameObject=ObjectLibrary.getObjectFromType(ObjectLibrary.idToType_[w.object]);
    var square:Square=new Square(map,int(w.x),int(w.y));square.obj_=wall;map.squares_[int(w.x)+int(w.y)*100]=square;wall.map_=map;wall.x_=w.x+.5;wall.y_=w.y+.5;wall.square_=square;wall.setObjectId(900+objects.length);objects.push(wall);
   }
   for each(var item:Object in c.items) {
    var type:int=ObjectLibrary.idToType_[item.object];
    if(type==0)throw new Error("Unknown object "+item.object);
    var go:GameObject=ObjectLibrary.getObjectFromType(type);
    if(go.obj3D_==null)throw new Error("Object has no software model "+item.object);
    go.setObjectId(id++);go.map_=map;go.x_=item.x;go.y_=item.y;
    go.obj3D_.setPosition(item.x,item.y,0,item.hasOwnProperty("rotation")?item.rotation:go.props_.rotation_);
    objects.push(go);
   }
   var graphics:Vector.<IGraphicsData>=new Vector.<IGraphicsData>();
   for each(go in objects)go.computeSortVal(camera);
   objects.sort(function(a:GameObject,b:GameObject):Number { return a.sortVal_==b.sortVal_ ? a.objectId_-b.objectId_ : a.sortVal_-b.sortVal_; });
   for each(go in objects) {
    var piece:Vector.<IGraphicsData>=new Vector.<IGraphicsData>();if(go.texture_==null&&go.randomTextureData_==null&&go.animatedChar_==null&&go.obj3D_!=null)go.obj3D_.draw(piece,camera,go.props_.color_,null);else go.draw(piece,camera,0);
    for each(var datum:IGraphicsData in piece)graphics.push(datum);
   }
   var holder:Sprite=new Sprite();holder.graphics.drawGraphicsData(graphics);
   var image:BitmapData=new BitmapData(800*c.scale,600*c.scale,false,0x363636);image.draw(holder,new Matrix(c.scale,0,0,c.scale,300*c.scale,325*c.scale));
   stream.open(out.resolvePath(c.id+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();
   report.push({id:c.id,scale:c.scale,angle:c.angle,items:c.items,walls:c.walls,graphics:graphics.length,debug:objects.length>0&&objects[0].obj3D_!=null?{faces:objects[0].obj3D_.faces_.length,vS:objects[0].obj3D_.vS_.slice(0,8),sortVal:objects[0].sortVal_}:null});
  }
  stream.open(out.resolvePath("states.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(report));stream.close();NativeApplication.nativeApplication.exit(0);
 } catch(error:Error) { try{ stream.open(new File(tasksOutput),FileMode.WRITE);stream.writeUTFBytes(error.getStackTrace());stream.close(); }catch(e2:Error){} NativeApplication.nativeApplication.exit(1); } }
}
}
