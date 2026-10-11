package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.objects.*;
import com.company.assembleegameclient.objects.particles.*;
import com.company.assembleegameclient.objects.thrown.ThrowProjectileEffect;
import com.company.assembleegameclient.map.Map;
import com.company.assembleegameclient.map.Camera;
import com.company.assembleegameclient.util.TextureRedrawer;
import com.company.assembleegameclient.sound.SoundEffectLibrary;
import com.company.util.AssetLibrary;
import kabam.rotmg.messaging.impl.data.WorldPosData;
import kabam.rotmg.core.StaticInjectorContext;
import kabam.rotmg.assets.services.CharacterFactory;
import kabam.rotmg.game.signals.AddTextLineSignal;
import org.swiftsuspenders.Injector;
import flash.display.*;
import flash.geom.*;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.*;
import flash.media.Sound;
import flash.utils.getQualifiedClassName;
public class ParticlesExporter extends Sprite {
 private var stream:FileStream=new FileStream();
 public function ParticlesExporter() { Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
 private function pos(x:Number,y:Number):WorldPosData { var p:WorldPosData=new WorldPosData();p.x_=x;p.y_=y;return p; }
 private function run(event:InvokeEvent):void { try {
  StaticInjectorContext.injector=new Injector();StaticInjectorContext.injector.map(AddTextLineSignal).toValue(new AddTextLineSignal());StaticInjectorContext.injector.map(CharacterFactory).toValue(new CharacterFactory());
  Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;Parameters.data_.particles=true;new ReferenceAssets().load();
  for each(var xml:XML in ObjectLibrary.xmlLibrary_) { SoundEffectLibrary.nameMap_[String(xml.HitSound)||"monster/default_hit"]=new Sound();SoundEffectLibrary.nameMap_[String(xml.DeathSound)||"monster/default_death"]=new Sound(); }
  stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();var out:File=new File(tasks.output);out.createDirectory();var result:Array=[];
  for each(var c:Object in tasks.cases) {
   var map:Map=new Map(null);map.setProps(100,100,"Particle reference",0,true,true);map.squares_.length=10000;
   var go:GameObject=ObjectLibrary.getObjectFromType(0x030e);go.objectId_=99;go.map_=map;go.x_=50;go.y_=50;
   var eff:ParticleEffect;var color:uint=0xff33ccff;var a:WorldPosData=pos(54,50),b:WorldPosData=pos(52,50);var props:EffectProperties;
   switch(c.type) {
    case 1:eff=new HealEffect(go,color);break;
    case 2:eff=new TeleportEffect();break;
    case 3:eff=new StreamEffect(pos(50,50),a,color);break;
    case 4:eff=new ThrowEffect(new Point(50,50),new Point(54,50),color,800);break;
    case 5:eff=new NovaEffect(go,3,color);break;
    case 6:eff=new PoisonEffect(go,color);break;
    case 7:eff=new LineEffect(go,a,color);break;
    case 8:eff=new BurstEffect(go,pos(50,50),pos(53,50),color);break;
    case 9:eff=new FlowEffect(a,go,color);break;
    case 10:eff=new RingEffect(go,3,color);break;
    case 11:eff=new LightningEffect(go,a,color,200);break;
    case 12:eff=new CollapseEffect(go,pos(50,50),pos(53,50),color);break;
    case 13:eff=new ConeBlastEffect(go,a,3,color);break;
    case 16:eff=new ThrowProjectileEffect(0xa22,new Point(50,50),new Point(54,50));break;
    case 17:eff=new AOEEffect(new Point(50,50),3,0xffff4444);break;
    case 18:eff=new HitEffect(new <uint>[0xFFCC2222,0xFF999966,0xFFCCCC99],100,10,0,100);break;
    case 19:eff=new ExplosionEffect(new <uint>[0xFFCC2222,0xFF999966,0xFFCCCC99],100,10);break;
    case 20:eff=new LevelUpEffect(go,0xff00ff00,20);break;
    case 21:eff=new FountainEffect(go);break;
    case 22:eff=new VentEffect(go);break;
    case 23:props=new EffectProperties(<Effect particle="SwirlingMist" cooldown="0.1">XMLEffect</Effect>);eff=new XMLEffect(go,props);break;
    case 24:props=new EffectProperties(<Effect color="0x00c631" rate="20" life=".45" lifeVariance=".1" speed="0" speedVariance=".5" size="5" rise="1" riseVariance=".1" rangeX="1.9" rangeY="1.9">CustomParticles</Effect>);eff=ParticleGenerator.attachParticleGenerator(props,go);break;
    case 25:eff=new HealingEffect(go);break;
   }
   var camera:Camera=new Camera();camera.configure(50,50,12,0,new Rectangle(-300,-325,800,600),false);
   eff.map_=map;eff.x_=eff.y_=50;eff.update(0,0);
   for(var time:int=17;time<=c.time;time+=17) {
    var live:Vector.<BasicObject>=map.boList_.concat();
    for each(var bo:BasicObject in live) {
     if(!bo.update(time,17))map.removeObj(bo.objectId_);
     else if(c.type==23) { bo.computeSortVal(camera);bo.draw(new Vector.<IGraphicsData>(),camera,time); }
    }
    if(c.type>=20&&c.type<=25)eff.update(time,17);
   }
   var states:Array=[];var graphics:Vector.<IGraphicsData>=new Vector.<IGraphicsData>();
   map.boList_.sort(function(x:BasicObject,y:BasicObject):Number { x.computeSortVal(camera);y.computeSortVal(camera);return x.sortVal_==y.sortVal_ ? x.objectId_-y.objectId_ : x.sortVal_-y.sortVal_; });
   for each(bo in map.boList_) {
    if(bo.z_<0)continue;
    bo.computeSortVal(camera);var piece:Vector.<IGraphicsData>=new Vector.<IGraphicsData>();bo.draw(piece,camera,c.time);
    for each(var datum:IGraphicsData in piece)graphics.push(datum);
    var state:Object={x:bo.x_,y:bo.y_,z:bo.z_,name:getQualifiedClassName(bo)};
    if(bo is Particle) { var p:Particle=bo as Particle;state.size=p.size_*20;state.color=p.color_; }
    else if(c.type==24) { state.size=5;state.color=0x00c631;state.custom=true; }
    else {
     var fill:GraphicsBitmapFill=piece[0] as GraphicsBitmapFill;state.texture=true;state.size=c.type==16 ? 80 : 100;state.bottom=c.type==23;state.file=c.type==16 ? "lofiObj3" : "lofiParts";state.index=0;
     if(c.type==16) { state.file="lofiObj2";state.index=50;state.rotation=.2; }
     if(c.type==23)for(var index:int=0;index<15;index++)if(fill.bitmapData.compare(TextureRedrawer.redraw(AssetLibrary.getImageFromSet("lofiParts",index),100,true,0))===0) { state.index=index;break; }
     if(c.type==23&&fill.bitmapData.compare(TextureRedrawer.redraw(AssetLibrary.getImageFromSet("invisible",0),100,true,0))===0) { state.file="invisible";state.index=0; }
    }
    states.push(state);
   }
   var holder:Sprite=new Sprite();holder.graphics.drawGraphicsData(graphics);
   var image:BitmapData=new BitmapData(800*c.scale,600*c.scale,false,0x363636);image.draw(holder,new Matrix(c.scale,0,0,c.scale,300*c.scale,325*c.scale));
   stream.open(out.resolvePath(c.id+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();
   result.push({id:c.id,type:c.type,time:c.time,scale:c.scale,particles:states});
  }
  stream.open(out.resolvePath("states.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(result));stream.close();NativeApplication.nativeApplication.exit(0);
 } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); } }
}
}
