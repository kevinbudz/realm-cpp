package {
import com.company.assembleegameclient.ui.StatusBar;
import com.company.ui.SimpleText;
import flash.display.Sprite;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.geom.Matrix;
import flash.filters.DropShadowFilter;
public class HudExporter extends Sprite {
    public function HudExporter() { NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,invoked); }
    private function invoked(event:InvokeEvent):void {
        try {
            var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);
            var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            var metrics:Array=[];
            for each(var task:Object in tasks.cases) {
                var hud:Sprite=new Sprite();hud.graphics.beginFill(3552822);hud.graphics.drawRect(0,0,200,600);hud.graphics.endFill();
                var name:SimpleText=new SimpleText(20,11776947);name.setBold(true);name.text=task.name;name.updateMetrics();name.filters=[new DropShadowFilter(0,0,0)];name.x=36;name.y=198;hud.addChild(name);
                var info:Object={id:task.id,bars:[]};
                for each(var data:Object in task.bars) {
                    var bar:StatusBar=new StatusBar(176,16,data.color,5526612,data.label);bar.x=12;bar.y=data.y;
                    bar.draw(data.value,data.maximum,data.boost,data.cap);hud.addChild(bar);
                    info.bars.push({value_width:bar.valueText_.width,value_x:bar.valueText_.x,boost_width:bar.boostText_.width,boost_x:bar.boostText_.x,text_color:bar.textColor_});
                }
                metrics.push(info);
                var image:BitmapData=new BitmapData(200,600,false,0);image.draw(hud);
                var file:File=new File(tasks.output+"/"+task.id+".png");file.parent.createDirectory();stream.open(file,FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose();
            }
            stream.open(new File(tasks.output+"/metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics));stream.close();
            NativeApplication.nativeApplication.exit(0);
        } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); }
    }
}
}
