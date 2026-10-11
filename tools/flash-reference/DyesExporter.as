package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.util.AnimatedChar;
import com.company.assembleegameclient.util.AnimatedChars;
import com.company.assembleegameclient.util.MaskedImage;
import com.company.assembleegameclient.util.TextureRedrawer;
import com.company.assembleegameclient.util.redrawers.GlowRedrawer;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.util.AssetLibrary;
import flash.display.Sprite;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
public class DyesExporter extends Sprite {
 private var stream:FileStream=new FileStream();
 public function DyesExporter() { NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
 private function save(file:File,im:BitmapData):void { file.parent.createDirectory();stream.open(file,FileMode.WRITE);stream.writeBytes(im.encode(im.rect,new PNGEncoderOptions()));stream.close(); }
 private function run(event:InvokeEvent):void {
  try {
   Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderFrame=true;new ReferenceAssets().load();
   stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();var out:File=new File(tasks.output);var metrics:Array=[];
   for each(var c:Object in tasks.cases) {
    var source:BitmapData;var mask:BitmapData=null;
    if(c.animated) { var character:AnimatedChar=AnimatedChars.getAnimatedChar(c.file,c.index);var frame:MaskedImage=character.imageFromDir(c.dir,c.action,c.phase);source=frame.image_;mask=frame.mask_; }
    else { source=AssetLibrary.getImageFromSet(c.file,c.index);if(c.maskFile)mask=AssetLibrary.getImageFromSet(c.maskFile,c.maskIndex); }
    save(out.resolvePath(c.key+"-source.png"),source);if(mask)save(out.resolvePath(c.key+"-mask.png"),mask);
    var result:BitmapData=GlowRedrawer.outlineGlow(TextureRedrawer.resize(source,mask,c.size,c.pad,c.tex1,c.tex2),0);
    save(out.resolvePath(c.key+".png"),result);metrics.push({key:c.key,width:result.width,height:result.height});result.dispose();
   }
   stream.open(out.resolvePath("metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics));stream.close();
  } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1);return; }
  NativeApplication.nativeApplication.exit(0);
 }
}
}
