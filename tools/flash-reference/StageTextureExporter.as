package {
import com.adobe.utils.AGALMiniAssembler;
import com.company.assembleegameclient.util.TextureRedrawer;
import com.company.assembleegameclient.parameters.Parameters;
import flash.display.Sprite;
import flash.display.Loader;
import flash.display.Bitmap;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.display3D.Context3D;
import flash.display3D.Context3DRenderMode;
import flash.display3D.Context3DProgramType;
import flash.display3D.Context3DTextureFormat;
import flash.display3D.Context3DVertexBufferFormat;
import flash.display3D.Context3DBlendFactor;
import flash.display3D.VertexBuffer3D;
import flash.display3D.IndexBuffer3D;
import flash.display3D.Program3D;
import flash.display3D.textures.Texture;
import flash.events.Event;
import flash.events.InvokeEvent;
import flash.events.IOErrorEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
import flash.net.URLRequest;
import flash.geom.Point;
import flash.geom.Rectangle;
import flash.geom.Vector3D;
import flash.geom.Matrix;
import flash.display.IGraphicsData;
import flash.display.GraphicsPath;
import flash.display.GraphicsBitmapFill;
import com.company.assembleegameclient.engine3d.Point3D;
import com.company.assembleegameclient.map.Camera;
import flash.utils.ByteArray;
public class StageTextureExporter extends Sprite {
private var tasks:Array, at:int=0, context:Context3D, loader:Loader;
public function StageTextureExporter() { Parameters.root=this;Parameters.GPURenderFrame=true; NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,invoked); }
private function invoked(event:InvokeEvent):void {
try {
var f:FileStream=new FileStream();f.open(new File(event.arguments[0]),FileMode.READ);tasks=JSON.parse(f.readUTFBytes(f.bytesAvailable)) as Array;f.close();
stage.stage3Ds[0].addEventListener(Event.CONTEXT3D_CREATE,created);stage.stage3Ds[0].requestContext3D(Context3DRenderMode.AUTO);
} catch(error:Error) { fail(error.getStackTrace()); }
}
private function created(event:Event):void { context=stage.stage3Ds[0].context3D; context.configureBackBuffer(800,600,0,false); next(); }
private function next():void {
if(at==tasks.length) { NativeApplication.nativeApplication.exit(0);return; }
loader=new Loader();loader.contentLoaderInfo.addEventListener(Event.COMPLETE,loaded);loader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR,function(e:IOErrorEvent):void { fail(e.text); });loader.load(new URLRequest(new File(tasks[at].input).url));
}
private function loaded(event:Event):void {
try {
var task:Object=tasks[at], original:BitmapData=(loader.content as Bitmap).bitmapData;
var image:BitmapData=task.processed ? original.clone() : TextureRedrawer.redraw(original,int(task.size),Boolean(task.padBottom),0,false);
var pw:int=1,ph:int=1;while(pw<image.width)pw*=2;while(ph<image.height)ph*=2;
var padded:BitmapData=new BitmapData(pw,ph,true,0);padded.copyPixels(image,image.rect,new Point());
var texture:Texture=context.createTexture(pw,ph,Context3DTextureFormat.BGRA,false);texture.uploadFromBitmapData(padded);
var v:VertexBuffer3D=context.createVertexBuffer(4,6);
var vertices:Vector.<Number>=new Vector.<Number>();
if(task.projectile) {
    Parameters.data_={projOutline:Boolean(task.outline)};
    var camera:Camera=new Camera();camera.configure(0,0,12,0,new Rectangle(-400,-300,800,600),false);
    var graphics:Vector.<IGraphicsData>=new Vector.<IGraphicsData>();
    new Point3D(8*Number(task.size)/100).draw(graphics,new Vector3D(),Number(task.angle),camera.wToS_,camera,image);
    var path:GraphicsPath=graphics[1] as GraphicsPath;
    var inverse:Matrix=(graphics[0] as GraphicsBitmapFill).matrix.clone();inverse.invert();
    for(var i:int=0;i<4;++i) {
        var point:Point=new Point(path.data[2*i],path.data[2*i+1]);
        var uv:Point=inverse.transformPoint(point);
        vertices.push((400+point.x)/800*2-1,1-(300+point.y)/600*2,0,1,uv.x/pw,uv.y/ph);
    }
} else {
    var x0:Number=8/800*2-1,y0:Number=1-8/600*2,x1:Number=(8+image.width)/800*2-1,y1:Number=1-(8+image.height)/600*2;
    vertices=new <Number>[x0,y0,0,1,0,0,x1,y0,0,1,image.width/pw,0,x1,y1,0,1,image.width/pw,image.height/ph,x0,y1,0,1,0,image.height/ph];
}
v.uploadFromVector(vertices,0,4);
var indices:IndexBuffer3D=context.createIndexBuffer(6);indices.uploadFromVector(new <uint>[0,1,2,0,2,3],0,6);
var assembler:AGALMiniAssembler=new AGALMiniAssembler();var program:Program3D=context.createProgram();
var vertexCode:Object=assembler.assemble(Context3DProgramType.VERTEX,"mov op, va0\nmov v0, va1\n");
var fragmentCode:Object=assembler.assemble(Context3DProgramType.FRAGMENT,"tex oc, v0, fs0 <2d,nearest,clamp>\n");program.upload(vertexCode as ByteArray,fragmentCode as ByteArray);
context.clear(0,0,0,1);context.setProgram(program);context.setTextureAt(0,texture);context.setVertexBufferAt(0,v,0,Context3DVertexBufferFormat.FLOAT_4);context.setVertexBufferAt(1,v,4,Context3DVertexBufferFormat.FLOAT_2);context.setBlendFactors(Context3DBlendFactor.SOURCE_ALPHA,Context3DBlendFactor.ONE_MINUS_SOURCE_ALPHA);context.drawTriangles(indices);
var capture:BitmapData=new BitmapData(800,600,false,0);context.drawToBitmapData(capture);
var output:File=new File(task.output);output.parent.createDirectory();var f:FileStream=new FileStream();f.open(output,FileMode.WRITE);f.writeBytes(capture.encode(capture.rect,new PNGEncoderOptions()));f.close();context.present();
context.setTextureAt(0,null);context.setVertexBufferAt(0,null);context.setVertexBufferAt(1,null);v.dispose();indices.dispose();program.dispose();texture.dispose();image.dispose();original.dispose();padded.dispose();capture.dispose();loader.unload();++at;next();
} catch(error:Error) { fail(error.getStackTrace()); }
}
private function fail(message:String):void { trace(message);NativeApplication.nativeApplication.exit(1); }
}
}
