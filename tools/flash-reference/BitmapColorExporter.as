package {
import flash.display.Sprite;
import flash.display.Loader;
import flash.display.Bitmap;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.geom.Point;
import flash.events.InvokeEvent;
import flash.events.Event;
import flash.events.IOErrorEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.net.URLRequest;
import flash.utils.ByteArray;
public class BitmapColorExporter extends Sprite {
    private var output:String;
    private var loader:Loader=new Loader();
    public function BitmapColorExporter() { NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,invoked); }
    private function invoked(event:InvokeEvent):void {
        output=event.arguments[1];loader.contentLoaderInfo.addEventListener(Event.COMPLETE,loaded);
        loader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR,function(event:IOErrorEvent):void { trace(event.text);NativeApplication.nativeApplication.exit(1); });
        loader.load(new URLRequest(new File(event.arguments[0]).url));
    }
    private function loaded(event:Event):void {
        try {
            var bitmap:BitmapData=Bitmap(loader.content).bitmapData;
            var bytes:ByteArray=new ByteArray();
            for(var y:int=0;y<bitmap.height;++y)for(var x:int=0;x<bitmap.width;++x)bytes.writeUnsignedInt(bitmap.getPixel32(x,y));
            var stream:FileStream=new FileStream();stream.open(new File(output),FileMode.WRITE);stream.writeBytes(bytes);stream.close();
            var black:BitmapData=new BitmapData(bitmap.width,bitmap.height,false,0);black.copyPixels(bitmap,bitmap.rect,new Point(),null,null,true);
            stream.open(new File(output+"-black.png"),FileMode.WRITE);stream.writeBytes(black.encode(black.rect,new PNGEncoderOptions()));stream.close();
            stream.open(new File(output+".png"),FileMode.WRITE);stream.writeBytes(bitmap.encode(bitmap.rect,new PNGEncoderOptions()));stream.close();
            NativeApplication.nativeApplication.exit(0);
        } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); }
    }
}
}
