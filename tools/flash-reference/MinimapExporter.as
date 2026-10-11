package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.map.GroundLibrary;
import com.company.assembleegameclient.map.Map;
import com.company.assembleegameclient.objects.ObjectLibrary;
import com.company.assembleegameclient.objects.GameObject;
import com.company.assembleegameclient.objects.Player;
import com.company.assembleegameclient.parameters.Parameters;
import kabam.rotmg.minimap.view.MiniMap;
import com.company.util.BitmapUtil;
import kabam.rotmg.core.StaticInjectorContext;
import kabam.rotmg.assets.services.CharacterFactory;
import kabam.rotmg.game.signals.AddTextLineSignal;
import org.swiftsuspenders.Injector;
import flash.display.Sprite;
import flash.media.Sound;
import com.company.assembleegameclient.sound.SoundEffectLibrary;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.events.InvokeEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
public class MinimapExporter extends Sprite {
    public function MinimapExporter() { Parameters.root=this;NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,invoked); }
    private function invoked(event:InvokeEvent):void {
        try {
            StaticInjectorContext.injector=new Injector();
            StaticInjectorContext.injector.map(AddTextLineSignal).toValue(new AddTextLineSignal());
            StaticInjectorContext.injector.map(CharacterFactory).toValue(new CharacterFactory());
            Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;new ReferenceAssets().load();
            var stream:FileStream=new FileStream();stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            for each(var soundXML:XML in ObjectLibrary.xmlLibrary_) {
                SoundEffectLibrary.nameMap_[String(soundXML.HitSound)||"monster/default_hit"]=new Sound();
                SoundEffectLibrary.nameMap_[String(soundXML.DeathSound)||"monster/default_death"]=new Sound();
            }
            var colors:Object={grounds:{},objects:{}};
            for(var type:* in GroundLibrary.xmlLibrary_)colors.grounds[type]=GroundLibrary.getColor(int(type));
            for(type in ObjectLibrary.xmlLibrary_) {
                var xml:XML=ObjectLibrary.xmlLibrary_[type];
                var texture:BitmapData=String(xml.Class)=="Wall" ? ObjectLibrary.typeToTopTextureData_[type].getTexture(0) : ObjectLibrary.typeToTextureData_[type].getTexture(0);
                colors.objects[type]=texture==null ? 0 : BitmapUtil.mostCommonColor(texture)&0xffffff;
            }
            var output:File=new File(tasks.output);output.createDirectory();stream.open(output.resolvePath("colors.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(colors));stream.close();
            for each(var task:Object in tasks.cases) {
                var definition:Object=tasks.maps ? tasks.maps[task.map] : task;
                var map:Map=new Map(null);map.setProps(definition.width,definition.height,"test",0,false,false);map.initialize();
                var mini:MiniMap=new MiniMap(192,192);mini.setMap(map);
                if(task.debug_arrow)for(var ay:int=0;ay<8;++ay)for(var ax:int=0;ax<8;++ax)mini.blueArrow_.setPixel32(ax,ay,0xff010000|((128+ax)<<8)|(128+ay));
                for each(var tile:Object in definition.tiles)mini.setGroundTile(tile.x,tile.y,tile.type);
                var focus:GameObject=new GameObject(null);focus.x_=task.focus[0];focus.y_=task.focus[1];mini.setFocus(focus);
                for each(var marker:Object in task.markers) {
                    var object:GameObject=ObjectLibrary.getObjectFromType(marker.type);object.setObjectId(marker.id);object.x_=marker.x;object.y_=marker.y;
                    if(object is Player)Player(object).isFellowGuild_=marker.fellow;
                    map.goDict_[marker.id]=object;
                }
                for(var z:int=0;z<task.zoom;++z)mini.zoomOut();
                Parameters.data_.cameraAngle=task.angle;mini.draw();
                var image:BitmapData=new BitmapData(192,192,false,0);image.draw(mini);
                stream.open(output.resolvePath(task.id+".png"),FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose();mini.dispose();
            }
            NativeApplication.nativeApplication.exit(0);
        } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1); }
    }
}
}
