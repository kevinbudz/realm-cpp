package {
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.map.HurtOverlay;
import com.company.assembleegameclient.map.GradientOverlay;
import com.company.assembleegameclient.map.Camera;
import com.company.assembleegameclient.background.*;
import com.company.util.AssetLibrary;
import com.company.util.ImageSet;
import com.company.assembleegameclient.util.ReferenceAssets;
import flash.display.*;
import flash.filters.*;
import flash.geom.*;
import flash.events.*;
import flash.net.URLRequest;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.*;
// Applies Map.as's software post effects to a source frame (the native world capture, so
// only the effects differ): BlurFilter(d,d) and BLIND_FILTER on the map sprite, then the
// real HurtOverlay (breath ColorTransform) and GradientOverlay above it, on black.
// BLIND_FILTER and BREATH_CT are protected statics in Map.as; their values are copied here.
public class PostFxExporter extends Sprite {
 private static const BLIND_FILTER:ColorMatrixFilter=new ColorMatrixFilter([0.05,0.05,0.05,0,0,0.05,0.05,0.05,0,0,0.05,0.05,0.05,0,0,0.05,0.05,0.05,1,0]);
 private static const BREATH_CT:ColorTransform=new ColorTransform(255/255,55/255,0/255,0);
 private var stream:FileStream=new FileStream();
 private var tasksOutput:String="";
 private var tasks:Object;
 private var out:File;
 private var index:int;
 public function PostFxExporter() { Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
 private function fail(error:Error):void { try{ stream.open(new File(tasksOutput),FileMode.WRITE);stream.writeUTFBytes(error.getStackTrace());stream.close(); }catch(e2:Error){} NativeApplication.nativeApplication.exit(1); }
 private function run(event:InvokeEvent):void { tasksOutput=event.arguments[0]+".error.txt";try {
  Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;Parameters.GPURenderFrame=false;Parameters.data_.GPURender=false;new ReferenceAssets().load();
  WebMain.sWidth=800;WebMain.sHeight=600;
  stream.open(new File(event.arguments[0]),FileMode.READ);tasks=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
  out=new File(tasks.output);out.createDirectory();index=0;next();
 } catch(error:Error) { fail(error); } }
 private function next():void {
  if(index>=tasks.cases.length) { stream.open(out.resolvePath("states.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(tasks.cases));stream.close();NativeApplication.nativeApplication.exit(0);return; }
  var c:Object=tasks.cases[index];
  if(c.backdrop=="stars"||c.backdrop=="nexus-real") { try { backdropReal(c);index++;next(); } catch(error:Error) { fail(error); } return; }
  var loader:Loader=new Loader();
  loader.contentLoaderInfo.addEventListener(Event.COMPLETE,function(e:Event):void { try { if(c.backdrop=="nexus-replica")nexusReplica(c,Bitmap(loader.content).bitmapData);else render(c,Bitmap(loader.content).bitmapData);index++;next(); } catch(error:Error) { fail(error); } });
  loader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR,function(e:IOErrorEvent):void { fail(new Error(e.text)); });
  loader.load(new URLRequest(new File(c.source).url));
 }
 // Backgrounds sit under the map at (-clipRect.x, -clipRect.y), on black, like Map.as places them.
 private function backdropCamera(c:Object):Camera { var camera:Camera=new Camera();camera.configure(50,50,12,c.angle,new Rectangle(-300,-325,800,600),false);return camera; }
 private function save(c:Object,holder:DisplayObject):void {
  var scene:Sprite=new Sprite();holder.x=300;holder.y=325;scene.addChild(holder);
  var image:BitmapData=new BitmapData(800,600,false,0);image.draw(scene);
  stream.open(out.resolvePath(c.id+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();
 }
 private function backdropReal(c:Object):void {
  var camera:Camera=backdropCamera(c);
  if(c.backdrop=="stars") {
   var stars:StarBackground=new StarBackground();stars.draw(camera,c.time);
   var set:ImageSet=AssetLibrary.getImageSet("stars");c.stars=[];
   for each(var star:Object in stars.stars_)c.stars.push({x:star.x_,y:star.y_,scale:star.scale_,image:set.images_.indexOf(star.bitmap_)});
   save(c,stars);
  } else {
   var nexus:NexusBackground=new NexusBackground();nexus.draw(camera,c.time);save(c,nexus);
  }
 }
 // NexusBackground.draw's fill matrix (translate by drift, then rotate by -angle) over a supplied bitmap.
 private function nexusReplica(c:Object,water:BitmapData):void {
  var shape:Shape=new Shape();var m:Matrix=new Matrix();m.translate(c.time*0.01,c.time*0.01);m.rotate(-c.angle);
  shape.graphics.beginBitmapFill(water,m,true,false);shape.graphics.drawRect(-300,-325,800,600);shape.graphics.endFill();save(c,shape);
 }
 private function render(c:Object,source:BitmapData):void {
  var clip:int=c.clip_width,h:int=c.height;
  var view:BitmapData=new BitmapData(clip,h,false,0);view.copyPixels(source,new Rectangle(0,0,clip,h),new Point(0,0));
  var map:Sprite=new Sprite();map.addChild(new Bitmap(view));
  var filters:Array=[];
  if(c.blur>0)filters.push(new BlurFilter(c.blur,c.blur));
  if(c.blind)filters.push(BLIND_FILTER);
  map.filters=filters;
  var scene:Sprite=new Sprite();scene.addChild(map);
  if(c.vignette>0) {
   var hurt:HurtOverlay=new HurtOverlay();BREATH_CT.alphaMultiplier=c.vignette;hurt.transform.colorTransform=BREATH_CT;hurt.visible=true;scene.addChild(hurt);
  }
  if(c.sidebar) { var gradient:GradientOverlay=new GradientOverlay();gradient.visible=true;gradient.x=clip-10;scene.addChild(gradient); }
  var image:BitmapData=new BitmapData(800,600,false,0);image.draw(scene);
  stream.open(out.resolvePath(c.id+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();
 }
}
}
