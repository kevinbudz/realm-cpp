package {
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
import flash.geom.Point;
import flash.geom.Rectangle;
import flash.filters.DropShadowFilter;
import flash.text.TextFormat;
import flash.utils.ByteArray;
public class FontExporter extends Sprite {
    private var bundle:FileStream;
    private var probe:Boolean;
    private var verticalPhase:Number;
    public function FontExporter() { NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE, invoked); }
    private function write(file:File, data:String):void {
        file.parent.createDirectory();var stream:FileStream=new FileStream();stream.open(file,FileMode.WRITE);stream.writeUTFBytes(data);stream.close();
    }
    private function png(file:File, image:BitmapData):void {
        file.parent.createDirectory();var stream:FileStream=new FileStream();stream.open(file,FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();
    }
    private function draw(text:SimpleText, file:File, horizontalShift:Number=0):Object {
        var image:BitmapData=new BitmapData(int(Math.ceil(text.width))+16,int(Math.ceil(text.height))+16,true,0);
        var matrix:Matrix=new Matrix();matrix.translate(8+horizontalShift,8+verticalPhase);image.draw(text,matrix);
        var bounds:Rectangle=image.getColorBoundsRect(0xff000000,0,false);
        var empty:Boolean=bounds.isEmpty();
        if(empty)bounds=new Rectangle(8,8,1,1);
        var cropped:BitmapData=new BitmapData(bounds.width,bounds.height,true,0);cropped.copyPixels(image,bounds,new Point());
        if(probe) {
            png(file,cropped);
            var high:BitmapData=new BitmapData(image.width*4,image.height*4,true,0), highAt:Matrix=new Matrix(4,0,0,4,32,32+verticalPhase*4);high.draw(text,highAt);
            var highCrop:BitmapData=new BitmapData(cropped.width*4,cropped.height*4,true,0);
            highCrop.copyPixels(high,new Rectangle(bounds.x*4,bounds.y*4,bounds.width*4,bounds.height*4),new Point());
            png(new File(file.nativePath+"-4x.png"),highCrop);high.dispose();highCrop.dispose();
        }
        var result:Object={x:bounds.x-8,y:bounds.y-8,empty:empty};
        if(bundle) {
            var bytes:ByteArray=new ByteArray();
            for(var y:int=0;y<cropped.height;++y)for(var x:int=0;x<cropped.width;++x)bytes.writeByte(cropped.getPixel32(x,y)>>>24);
            bytes.compress();result.width=cropped.width;result.height=cropped.height;
            result.offset=bundle.position;result.bytes=bytes.length;bundle.writeBytes(bytes);
        } else png(file,cropped);
        image.dispose();cropped.dispose();return result;
    }
    private function invoked(event:InvokeEvent):void {
        try {
            var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);
            var input:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            var output:File=new File(input.output), manifest:Object={schema:1,source_sha256:input.source_sha256,fonts:[]};
            probe=Boolean(input.probe);
            if(input.bundle) { bundle=new FileStream();bundle.open(output.resolvePath("glyphs.bin"),FileMode.WRITE);manifest.bundle="glyphs.bin";manifest.bundle_format="alpha-zlib"; }
            var impulse:BitmapData=new BitmapData(32,32,true,0);impulse.setPixel32(16,16,0xffffffff);
            var filtered:BitmapData=impulse.clone();filtered.applyFilter(impulse,impulse.rect,new Point(),new DropShadowFilter(0,0,0));
            png(output.resolvePath("shadow-impulse.png"),filtered);filtered.dispose();impulse.dispose();
            for each(var font:Object in input.fonts) {
                verticalPhase=Number(font.vertical_phase)||0;
                var field:SimpleText=new SimpleText(font.size,0xffffff);field.setBold(font.bold);
                if(font.shadow)field.filters=[new DropShadowFilter(0,0,0)];
                var phases:int=input.phases || 1;
                var result:Object={id:font.id,size:font.size,bold:font.bold,shadow:font.shadow,vertical_phase:verticalPhase,phases:phases,glyphs:[],kerning:[]};
                var widths:Object={};
                field.text=" ";field.updateMetrics();var spaceAdvance:Number=field.getLineMetrics(0).width;
                var lineAdvance:Number=field.getLineMetrics(0).height;
                for each(var code:int in font.codes) {
                    var character:String=String.fromCharCode(code);field.text=character;field.setTextFormat(field.defaultTextFormat);field.updateMetrics();
                    var advance:Number=field.getLineMetrics(0).width;widths[code]=advance;
                    if(!result.hasOwnProperty("line_height")) { result.line_height=field.actualHeight_;result.line_advance=lineAdvance; }
                    for(var phase:int=0;phase<(font.shadow && bundle ? 0 : phases);++phase) {
                        field.text=" "+character;field.setTextFormat(field.defaultTextFormat);
                        var prefix:TextFormat=field.getTextFormat(0,1);prefix.letterSpacing=32+phase/20-spaceAdvance;field.setTextFormat(prefix,0,1);field.updateMetrics();
                        // A glyph in the middle of a field can overhang its
                        // advance. A one-character field otherwise clips it.
                        field.width += 8;
                        var filename:String=font.id+"/"+code+"-"+phase+".png";
                        var glyph:Object=draw(field,output.resolvePath(filename));glyph.x-=32;glyph.code=code;glyph.phase=phase;
                        var charBounds:Rectangle=field.getCharBoundaries(1);glyph.actual_phase=charBounds ? charBounds.x-34 : null;
                        glyph.advance=advance;if(!bundle)glyph.file=filename;result.glyphs.push(glyph);
                    }
                    if(!font.shadow) {
                        field.text=character;field.setTextFormat(field.defaultTextFormat);field.updateMetrics();field.width+=8;
                        var initialFile:String=font.id+"/"+code+"-initial.png";
                        var initial:Object=draw(field,output.resolvePath(initialFile));initial.code=code;initial.phase=20;initial.advance=advance;
                        if(!bundle)initial.file=initialFile;result.glyphs.push(initial);
                        var halfFile:String=font.id+"/"+code+"-initial-half.png";
                        var half:Object=draw(field,output.resolvePath(halfFile),.5);half.code=code;half.phase=21;half.advance=advance;
                        if(!bundle)half.file=halfFile;result.glyphs.push(half);
                    }
                }
                for each(var left:int in font.kern_codes)for each(var right:int in font.kern_codes) {
                    field.text=String.fromCharCode(left)+String.fromCharCode(right);field.setTextFormat(field.defaultTextFormat);field.updateMetrics();
                    var delta:Number=field.getLineMetrics(0).width-widths[left]-widths[right];
                    if(Math.abs(delta)>.0001)result.kerning.push([left,right,delta]);
                }
                manifest.fonts.push(result);
                for each(var sample:String in input.samples) {
                    field.text=sample;field.setTextFormat(field.defaultTextFormat);field.updateMetrics();
                    var reference:BitmapData=new BitmapData(800,600,false,0);var at:Matrix=new Matrix();at.translate(8,8+verticalPhase);reference.draw(field,at);
                    png(output.resolvePath("references/"+font.id+"/"+input.samples.indexOf(sample)+".png"),reference);reference.dispose();
                    var transparent:BitmapData=new BitmapData(800,600,true,0);transparent.draw(field,at);
                    png(output.resolvePath("transparent/"+font.id+"/"+input.samples.indexOf(sample)+".png"),transparent);transparent.dispose();
                }
            }
            if(bundle)bundle.close();write(output.resolvePath("manifest.json"),JSON.stringify(manifest));
            NativeApplication.nativeApplication.exit(0);
        } catch(error:Error) { write(new File(event.arguments[0]).parent.resolvePath("font-error.log"),"font="+(font ? font.id : "")+" code="+code+" phase="+phase+"\n"+error.toString()+"\n"+error.getStackTrace());trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); }
    }
}
}
