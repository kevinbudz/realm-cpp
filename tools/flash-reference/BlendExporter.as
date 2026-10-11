package {
import flash.display.Sprite;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.geom.Point;
public class BlendExporter extends Sprite {
public function BlendExporter() { NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
private function run(event:InvokeEvent):void {
try {
var fs:FileStream=new FileStream();fs.open(new File(event.arguments[0]),FileMode.READ);var cases:Array=JSON.parse(fs.readUTFBytes(fs.bytesAvailable)) as Array;fs.close();
var source:BitmapData=new BitmapData(1,1,true,0), target:BitmapData=new BitmapData(1,1,true,0),mask:BitmapData=new BitmapData(1,1,true,0); var zero:Point=new Point();var results:Array=[]; var encoded:BitmapData = new BitmapData(cases.length,1,true,0); var at:int=0;
for each(var c:Object in cases) { source.setPixel32(0,0,uint(c.src)); target.setPixel32(0,0,uint(c.dst));mask.setPixel32(0,0,uint(c.mask)<<24);target.copyPixels(source,source.rect,zero,mask,zero,true);results.push(target.getPixel32(0,0));encoded.copyPixels(target,target.rect,new Point(at++,0)); }
fs.open(new File(event.arguments[1]),FileMode.WRITE);fs.writeUTFBytes(JSON.stringify(results));fs.close();fs.open(new File(event.arguments[1]+".png"),FileMode.WRITE);fs.writeBytes(encoded.encode(encoded.rect,new PNGEncoderOptions()));fs.close();NativeApplication.nativeApplication.exit(0);
} catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); }
}
}
}
