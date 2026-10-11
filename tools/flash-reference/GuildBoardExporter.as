package {
import com.company.assembleegameclient.ui.board.ViewBoard;
import com.company.assembleegameclient.ui.board.EditBoard;
import com.company.assembleegameclient.ui.Scrollbar;
import com.company.assembleegameclient.ui.TextButton;
import com.company.ui.SimpleText;
import flash.display.Sprite;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.display.StageScaleMode;
import flash.display.StageAlign;
import flash.geom.Matrix;
import flash.events.InvokeEvent;
import flash.events.MouseEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
public class GuildBoardExporter extends Sprite {
    public function GuildBoardExporter() {
        stage.scaleMode=StageScaleMode.NO_SCALE;stage.align=StageAlign.TOP_LEFT;
        NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run);
    }
    private function run(event:InvokeEvent):void {
        try {
            new SimpleText(16,0xffffff);
            var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);
            var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            var output:File=new File(tasks.output);output.createDirectory();var metrics:Array=[];
            for each(var c:Object in tasks.cases) {
                var board:Sprite=c.edit ? new EditBoard(c.text) : new ViewBoard(c.text,c.rank>=20);
                addChild(board);
                var w:Number=Object(board).w_,h:Number=Object(board).h_;
                for(var i:int=0;i<board.numChildren;++i) {
                    var child:Object=board.getChildAt(i);
                    if(child is Scrollbar)Scrollbar(child).setPos(c.scroll);
                    if(child is TextButton&&c.hover>=0&&child.x==(c.hover==0 ? 4 : w-124))child.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER));
                }
                var s:Number=c.height/600;var image:BitmapData=new BitmapData(c.width,c.height,true,0xff363636);
                var dim:Sprite=new Sprite();dim.graphics.beginFill(0,.8);dim.graphics.drawRect(0,0,c.width,c.height);image.draw(dim);
                image.draw(board,new Matrix(s,0,0,s,(c.width-w*s)/2,(c.height-h*s)/2));
                stream.open(output.resolvePath(c.key+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose();
                metrics.push({key:c.key,width:w,height:h});removeChild(board);
            }
            stream.open(output.resolvePath("metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics,null,2));stream.close();
        } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1);return; }
        NativeApplication.nativeApplication.exit(0);
    }
}
}
