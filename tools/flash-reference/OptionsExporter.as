package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.ui.options.Options;
import com.company.assembleegameclient.ui.options.OptionsTabTitle;
import com.company.assembleegameclient.ui.options.Option;
import com.company.assembleegameclient.ui.options.KeyCodeBox;
import com.company.assembleegameclient.ui.options.ChoiceBox;
import com.company.assembleegameclient.screens.TitleMenuOption;
import com.company.assembleegameclient.ui.tooltip.ToolTip;
import com.company.assembleegameclient.ui.tooltip.TextToolTip;
import com.company.ui.SimpleText;
import flash.display.Sprite;
import flash.display.DisplayObject;
import flash.display.DisplayObjectContainer;
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
// Renders the real Options screen: every tab, hover states, disabled rows and a tooltip, at several window sizes.
public class OptionsExporter extends Sprite {
    private var stream:FileStream=new FileStream();
    public function OptionsExporter() {
        stage.scaleMode=StageScaleMode.NO_SCALE;stage.align=StageAlign.TOP_LEFT;
        NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run);
    }
    private function save(file:File,image:BitmapData):void { file.parent.createDirectory();stream.open(file,FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose(); }
    private function find(root:DisplayObjectContainer,type:Class,out:Array):void {
        for(var i:int=0;i<root.numChildren;++i) { var child:DisplayObject=root.getChildAt(i);if(child is type)out.push(child);if(child is DisplayObjectContainer)find(DisplayObjectContainer(child),type,out); }
    }
    private function run(event:InvokeEvent):void {
        try {
            Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;new ReferenceAssets().load();new SimpleText(14,0xffffff);
            stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            var metrics:Array=[];
            for each(var c:Object in tasks.cases) {
                Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;
                for(var name:String in c.values)Parameters.data_[name]=c.values[name];
                stage.nativeWindow.width=c.width+(stage.nativeWindow.width-stage.stageWidth);stage.nativeWindow.height=c.height+(stage.nativeWindow.height-stage.stageHeight);
                var options:Options=new Options(null);addChild(options);
                var tabs:Array=[];find(options,OptionsTabTitle,tabs);
                if(c.tab>0)tabs[c.tab].dispatchEvent(new MouseEvent(MouseEvent.CLICK));
                var boxes:Array=[];find(options,Option,boxes);
                var rows:Array=[];for each(var option:Option in boxes)rows.push(option);
                // Rows added to a tab are children of the content layer in grid order; spacers are plain Sprites.
                var content:DisplayObjectContainer=tabs[0].parent;
                var cells:Array=[];for(var k:int=0;k<content.numChildren;++k) { var cell:DisplayObject=content.getChildAt(k);if(cell is Sprite&&!(cell is OptionsTabTitle)&&!(cell is TitleMenuOption)&&cell.y>=122)cells.push(cell); }
                cells.sort(function(a:DisplayObject,b:DisplayObject):int { return a.y!=b.y ? a.y-b.y : a.x-b.x; });
                var tip:ToolTip=null;
                if(c.hasOwnProperty("hover")) {
                    var h:Object=c.hover,target:DisplayObject=null;
                    if(h.kind=="tab")target=tabs[h.index];
                    else if(h.kind=="button") { var btns:Array=[];find(options,TitleMenuOption,btns);btns.sortOn("x",Array.NUMERIC);target=btns[h.index==300 ? 1 : h.index==301 ? 0 : 2]; }
                    else if(h.kind=="box") { var row:Sprite=cells[h.index];target=row.getChildAt(row.numChildren-1==1 ? 0 : 0); for(var q:int=0;q<row.numChildren;++q)if(row.getChildAt(q) is KeyCodeBox||row.getChildAt(q) is ChoiceBox)target=row.getChildAt(q); }
                    else if(h.kind=="label") { var lr:Sprite=cells[h.index];target=lr.getChildAt(0); }
                    if(target!=null)target.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER));
                    if(h.kind=="label")tip=stage.getChildAt(stage.numChildren-1) as ToolTip;
                }
                var s:Number=c.height/600;
                var image:BitmapData=new BitmapData(c.width,c.height,false,0x363636);
                image.draw(options);
                if(tip!=null) {
                    // ToolTip.position() for a pointer 16 design units into the label's row, in stage pixels.
                    var lrow:Sprite=cells[c.hover.index];var mx:Number=(c.width-800*s)/2+(lrow.x+120)*s,my:Number=(lrow.y+16)*s;
                    tip.x=mx<c.width/2 ? mx+12 : mx-tip.width-1;if(tip.x<12)tip.x=12;
                    tip.y=my<c.height/3 ? my+12 : my-tip.height-1;if(tip.y<12)tip.y=12;
                    image.draw(tip,new Matrix(tip.scaleX,0,0,tip.scaleY,tip.x,tip.y));stage.removeChild(tip);
                }
                save(new File(tasks.output+"/"+c.key+".png"),image);
                var rects:Array=[];
                for each(var r:Sprite in cells)rects.push({x:r.x,y:r.y,w:r.width,h:r.height});
                metrics.push({key:c.key,stage:[stage.stageWidth,stage.stageHeight],cells:rects.length,rects:rects});
                removeChild(options);
            }
            stage.nativeWindow.width=800+(stage.nativeWindow.width-stage.stageWidth);stage.nativeWindow.height=600+(stage.nativeWindow.height-stage.stageHeight);
            // TextToolTip as IconButton and Option build it, placed by ToolTip.position() for a pointer at the origin.
            for each(var t:Object in tasks.tooltips) {
                var tt:TextToolTip=new TextToolTip(t.background,t.outline,t.title=="" ? null : t.title,t.text,t.width);
                stage.addChild(tt);
                var tipImage:BitmapData=new BitmapData(300,200,false,0x363636);
                tipImage.draw(tt,new Matrix(tt.scaleX,0,0,tt.scaleY,tt.x,tt.y));
                save(new File(tasks.output+"/"+t.key+".png"),tipImage);
                metrics.push({key:t.key,stage:[tt.x,tt.y],width:tt.width,height:tt.height,title:tt.titleText_==null ? null : [tt.titleText_.x,tt.titleText_.y,tt.titleText_.width,tt.titleText_.height,tt.titleText_.textWidth,tt.titleText_.textHeight],tip:[tt.tipText_.x,tt.tipText_.y,tt.tipText_.width,tt.tipText_.height,tt.tipText_.textWidth,tt.tipText_.textHeight]});
                stage.removeChild(tt);
            }
            stream.open(new File(tasks.output+"/metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics,null,2));stream.close();
        } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1);return; }
        NativeApplication.nativeApplication.exit(0);
    }
}
}
