package {
import com.company.ui.SimpleText;
import flash.display.Sprite;
import flash.display.BitmapData;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.geom.Matrix;
import flash.text.TextFieldAutoSize;
import flash.text.TextLineMetrics;
public class WrapProbe extends Sprite {
    public function WrapProbe() { NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE, invoked); }
    private function field(size:int,width:int,text:String):SimpleText {
        var t:SimpleText=new SimpleText(size,0xffffff,false,width,0);t.setBold(true);
        t.htmlText="<p align=\"center\">"+text+"</p>";t.wordWrap=true;t.multiline=true;t.autoSize=TextFieldAutoSize.CENTER;
        t.htmlText="<p align=\"center\">"+text+"</p>";return t;
    }
    private function render(t:SimpleText,dy:Number):BitmapData {
        var image:BitmapData=new BitmapData(t.width+16,200,true,0);var m:Matrix=new Matrix();m.translate(8,8+dy);image.draw(t,m);return image;
    }
    private function invoked(event:InvokeEvent):void {
        var out:Array=[];
        try {
            var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);
            var input:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            for each(var probe:Object in input.probes) {
                var multi:SimpleText=field(probe.size,probe.width,probe.text);var whole:BitmapData=render(multi,0);
                var entry:Object={size:probe.size,text:probe.text,height:multi.height,lines:[]};
                for(var i:int=0;i<multi.numLines;++i) {
                    var lm:TextLineMetrics=multi.getLineMetrics(i);var line:String=multi.getLineText(i).replace(/[\r\n]/g,"");
                    if(line.length==0)continue;
                    var single:SimpleText=field(probe.size,probe.width,line);
                    var top:int=8+int(i*(lm.ascent+lm.descent+lm.leading))+2, bottom:int=top+int(lm.ascent+lm.descent)-4;
                    var scores:Array=[];
                    for(var tw:int=0;tw<=int(probe.span);++tw) {
                        var dy:Number=(int(probe.base)*i+tw)/20;var candidate:BitmapData=render(single,dy);var diff:int=0;
                        for(var y:int=top;y<bottom;++y)for(var x:int=0;x<whole.width;++x)if(whole.getPixel32(x,y)!=candidate.getPixel32(x,y))++diff;
                        scores.push(diff);candidate.dispose();
                    }
                    entry.lines.push({i:i,text:line,ascent:lm.ascent,descent:lm.descent,leading:lm.leading,scores:scores});
                }
                out.push(entry);whole.dispose();
            }
            var o:FileStream=new FileStream();o.open(new File(input.output),FileMode.WRITE);o.writeUTFBytes(JSON.stringify(out));o.close();
        } catch(e:Error) {
            var err:FileStream=new FileStream();err.open(new File(event.arguments[0]+".error.txt"),FileMode.WRITE);err.writeUTFBytes(e.getStackTrace());err.close();
        }
        NativeApplication.nativeApplication.exit(0);
    }
}
}
