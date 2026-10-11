package {
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.util.ConditionEffect;
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.ui.SimpleText;
import flash.display.*;
import flash.filters.GlowFilter;
import flash.geom.*;
import flash.events.*;
import flash.desktop.NativeApplication;
import flash.filesystem.*;
// Exports the real status art (task 11-status-effects): ConditionEffect's cached 16x16 condition
// icons (GlowRedrawer outline + glow, drawn by the real class), CharacterStatusText's text sprite
// (SimpleText 24 pt bold + GlowFilter(0,1,4,4,2,1), built exactly as its constructor does; the
// constructor itself needs a live GameObject) and GameObject.generateNameText/BitmapData for names.
public class StatusExporter extends Sprite {
 private var stream:FileStream=new FileStream();
 private var errorPath:String="";
 public function StatusExporter() { Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
 private function fail(error:Error):void { try{ stream.open(new File(errorPath),FileMode.WRITE);stream.writeUTFBytes(error.getStackTrace());stream.close(); }catch(e2:Error){} NativeApplication.nativeApplication.exit(1); }
 private function save(out:File,name:String,image:BitmapData):void {
  stream.open(out.resolvePath(name+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();
 }
 private function run(event:InvokeEvent):void { errorPath=event.arguments[0]+".error.txt";try {
  Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;Parameters.GPURenderFrame=false;Parameters.data_.GPURender=false;new ReferenceAssets().load();
  stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
  var out:File=new File(tasks.output);out.createDirectory();
  var metrics:Object={icons:[],texts:[],names:[]};
  for(var id:int=1;id<ConditionEffect.effects_.length;id++) {
   var effect:ConditionEffect=ConditionEffect.effects_[id];
   if(effect.iconOffsets_==null)continue;
   for(var index:int=0;index<effect.iconOffsets_.length;index++) {
    var icons:Vector.<BitmapData>=new Vector.<BitmapData>();
    ConditionEffect.getConditionEffectIcons(effect.bit_,icons,index);
    save(out,"icon-"+id+"-"+index,icons[0]);metrics.icons.push({id:id,index:index,frame:effect.iconOffsets_[index]});
   }
  }
  for each(var c:Object in tasks.texts) {
   var t:SimpleText=new SimpleText(24,c.color,false,0,0);t.setBold(true);t.text=c.text;t.updateMetrics();
   t.filters=[new GlowFilter(0,1,4,4,2,1)];t.x=-t.width/2;t.y=-t.height/2;
   var holder:Sprite=new Sprite();holder.addChild(t);
   var canvas:BitmapData=new BitmapData(240,80,true,0);canvas.draw(holder,new Matrix(1,0,0,1,120,40));
   save(out,"text-"+c.id,canvas);metrics.texts.push({id:c.id,width:t.width,height:t.height});
  }
  for each(var n:Object in tasks.names) {
   var nameText:SimpleText=new SimpleText(16,16777215,false,0,0);nameText.setBold(true);nameText.text=n.text;nameText.updateMetrics();
   var bitmap:BitmapData=new BitmapData(nameText.width,64,true,0);bitmap.draw(nameText,null);
   bitmap.applyFilter(bitmap,bitmap.rect,new Point(0,0),new GlowFilter(0,1,3,3,2,1));
   save(out,"name-"+n.id,bitmap);metrics.names.push({id:n.id,width:nameText.width,height:nameText.height});
  }
  stream.open(out.resolvePath("metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics));stream.close();
  NativeApplication.nativeApplication.exit(0);
 } catch(error:Error) { fail(error); } }
}
}
