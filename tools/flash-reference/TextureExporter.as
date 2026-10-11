package {
import com.company.assembleegameclient.util.TextureRedrawer;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.util.redrawers.GlowRedrawer;
import flash.display.Sprite;
import flash.display.Loader;
import flash.display.Bitmap;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.events.Event;
import flash.events.InvokeEvent;
import flash.events.IOErrorEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.net.URLRequest;
public class TextureExporter extends Sprite {
    private var tasks:Array;
    private var at:int = 0;
    private var loader:Loader;
    public function TextureExporter() {
        Parameters.root = this;
        Parameters.GPURenderFrame = true;
        NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE, invoked);
    }
    private function invoked(event:InvokeEvent):void {
        try {
            if (event.arguments.length != 1) throw new Error("Expected task JSON path");
            var stream:FileStream = new FileStream();
            stream.open(new File(event.arguments[0]), FileMode.READ);
            tasks = JSON.parse(stream.readUTFBytes(stream.bytesAvailable)) as Array;
            stream.close();
            next();
        } catch (error:Error) { fail(error.toString()); }
    }
    private function next():void {
        if (at == tasks.length) { trace("Exported " + at + " reference textures"); NativeApplication.nativeApplication.exit(0); return; }
        loader = new Loader();
        loader.contentLoaderInfo.addEventListener(Event.COMPLETE, loaded);
        loader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR, function(e:IOErrorEvent):void { fail(e.text); });
        loader.load(new URLRequest(new File(tasks[at].input).url));
    }
    private function loaded(event:Event):void {
        try {
            var task:Object = tasks[at];
            var image:BitmapData = (loader.content as Bitmap).bitmapData;
            var result:BitmapData = TextureRedrawer.redraw(image, int(task.size), Boolean(task.padBottom), uint(task.glow), false);
            var file:File = new File(task.output); file.parent.createDirectory();
            var stream:FileStream = new FileStream(); stream.open(file, FileMode.WRITE);
            stream.writeBytes(result.encode(result.rect, new PNGEncoderOptions())); stream.close();
            result.dispose(); image.dispose(); loader.unload();
            ++at;
            next();
        } catch (error:Error) { fail(error.toString()); }
    }
    private function fail(message:String):void { trace("TextureExporter: " + message); NativeApplication.nativeApplication.exit(1); }
}
}
