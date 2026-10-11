package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.util.TileRedrawer;
import com.company.assembleegameclient.map.GroundLibrary;
import com.company.assembleegameclient.map.Map;
import com.company.assembleegameclient.map.Square;
import com.company.assembleegameclient.parameters.Parameters;
import com.hurlant.util.Base64;
import flash.display.Sprite;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.utils.ByteArray;
public class GroundExporter extends Sprite {
    public function GroundExporter() {
        Parameters.root = this;
        NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE, invoked);
    }
    private function read(path:String):String {
        var stream:FileStream = new FileStream(); stream.open(new File(path),FileMode.READ);
        var text:String = stream.readUTFBytes(stream.bytesAvailable); stream.close(); return text;
    }
    private function invoked(event:InvokeEvent):void {
        try {
            Parameters.data_ = {}; Parameters.setDefaults(); Parameters.GPURenderError = true;
            new ReferenceAssets().load();
            var jm:Object = JSON.parse(read(event.arguments[0]));
            var map:Map = new Map(null); map.setProps(jm.width,jm.height,"test",0,false,false); map.initialize();
            var data:ByteArray = Base64.decodeToByteArray(jm.data); data.uncompress();
            for (var y:int = 0; y < jm.height; ++y) for (var x:int = 0; x < jm.width; ++x) {
                var entry:Object = jm.dict[data.readShort()]; if (entry.hasOwnProperty("ground")) map.setGroundTile(x,y,GroundLibrary.idToType_[entry.ground]);
            }
            var tasks:Array = JSON.parse(read(event.arguments[1])) as Array;
            for each (var task:Object in tasks) {
                var square:Square = map.lookupSquare(task.x,task.y);
                var image:BitmapData = task.mode == "base" ? square.texture_ : TileRedrawer.redraw(square,task.mode != "overlay");
                if (image == null) image = square.texture_;
                var output:File = new File(task.output); output.parent.createDirectory();
                var stream:FileStream = new FileStream(); stream.open(output,FileMode.WRITE); stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();
            }
            trace("Exported " + tasks.length + " terrain references"); NativeApplication.nativeApplication.exit(0);
        } catch (error:Error) { trace(error.getStackTrace()); NativeApplication.nativeApplication.exit(1); }
    }
}
}
