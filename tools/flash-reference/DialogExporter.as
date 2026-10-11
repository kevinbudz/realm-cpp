package {
import com.company.assembleegameclient.ui.dialogs.Dialog;
import kabam.rotmg.ui.view.MessageCloseDialog;
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
public class DialogExporter extends Sprite {
    public function DialogExporter() {
        stage.scaleMode=StageScaleMode.NO_SCALE;stage.align=StageAlign.TOP_LEFT;
        NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run);
    }
    private function run(event:InvokeEvent):void {
        try {
            new SimpleText(14,0xffffff);
            var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);
            var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            var output:File=new File(tasks.output);output.createDirectory();var metrics:Array=[];
            for each(var c:Object in tasks.cases) {
                var dialog:Dialog=c.messageClose ? new MessageCloseDialog(c.title,c.body,c.buttons[0]||"") : new Dialog(c.body,c.title,c.buttons[0],c.buttons[1]);
                addChild(dialog);
                if(c.disabled>=0) (c.disabled==0 ? dialog.button1_ : dialog.button2_).setEnabled(false);
                if(c.hover>=0) (c.hover==0 ? dialog.button1_ : dialog.button2_).dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER));
                var h:Number=dialog.rect_.height-1;
                var s:Number=c.height/600;
                var image:BitmapData=new BitmapData(c.width,c.height,true,0xff363636);
                if(c.backdrop) { var dim:Sprite=new Sprite();dim.graphics.beginFill(0x151515,.6);dim.graphics.drawRect(0,0,c.width,c.height);image.draw(dim); }
                // Explicit target stage dimensions, using the real Dialog layout's
                // outline-inclusive box bounds (301 x frameHeight+1).
                image.draw(dialog.box_,new Matrix(s,0,0,s,(c.width-dialog.box_.width*s)/2,(c.height-dialog.box_.height*s)/2));
                stream.open(output.resolvePath(c.key+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose();
                metrics.push({key:c.key,height:h,boxWidth:dialog.box_.width,boxHeight:dialog.box_.height,bodyY:dialog.textText_.y,bodyHeight:dialog.textText_.height,bodyTextHeight:dialog.textText_.textHeight,buttonY:dialog.button1_ ? dialog.button1_.y : -1,buttonHeight:dialog.button1_ ? dialog.button1_.height : 0,titleHeight:dialog.titleText_ ? dialog.titleText_.height : 0});
                removeChild(dialog);
            }
            stream.open(output.resolvePath("metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics,null,2));stream.close();
        } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1);return; }
        NativeApplication.nativeApplication.exit(0);
    }
}
}
