package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.util.GuildUtil;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.ui.guild.GuildPlayerList;
import com.company.assembleegameclient.ui.Scrollbar;
import com.company.assembleegameclient.ui.layout.MenuFrame;
import com.company.assembleegameclient.screens.TitleMenuOption;
import com.company.rotmg.graphics.DeleteXGraphic;
import com.company.ui.SimpleText;
import com.company.assembleegameclient.ui.dialogs.Dialog;
import kabam.rotmg.core.StaticInjectorContext;
import kabam.rotmg.account.core.Account;
import kabam.rotmg.appengine.api.AppEngineClient;
import org.swiftsuspenders.Injector;
import flash.display.Sprite;
import flash.display.DisplayObject;
import flash.display.BitmapData;
import flash.display.PNGEncoderOptions;
import flash.geom.Matrix;
import flash.events.InvokeEvent;
import flash.events.MouseEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
public class GuildMembersExporter extends Sprite {
    private var stream:FileStream=new FileStream();
    public function GuildMembersExporter() { NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
    private function save(file:File,image:BitmapData):void { file.parent.createDirectory();stream.open(file,FileMode.WRITE);stream.writeBytes(image.encode(image.rect,new PNGEncoderOptions()));stream.close();image.dispose(); }
    private function run(event:InvokeEvent):void {
        try {
            Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;new ReferenceAssets().load();new SimpleText(22,0xffffff);
            StaticInjectorContext.injector=new Injector();StaticInjectorContext.injector.map(Account).toValue(new FixtureAccount());
            stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
            var icons:Array=[GuildUtil.guildFameIcon(40)];for each(var r:int in [0,10,20,30,40])icons.push(GuildUtil.rankToIcon(r,20));
            for(var k:int=0;k<6;++k) { var im:BitmapData=icons[k];save(new File(tasks.art+"/"+k+".png"),im.clone()); }
            var cross:DeleteXGraphic=new DeleteXGraphic();var xImage:BitmapData=new BitmapData(Math.ceil(cross.width),Math.ceil(cross.height),true,0);xImage.draw(cross);save(new File(tasks.art+"/6.png"),xImage);
            var metrics:Array=[];
            for each(var c:Object in tasks.cases) {
                var client:FixtureClient=new FixtureClient();StaticInjectorContext.injector=new Injector();StaticInjectorContext.injector.map(Account).toValue(new FixtureAccount());StaticInjectorContext.injector.map(AppEngineClient).toValue(client);
                var list:GuildPlayerList=new GuildPlayerList(50,0,c.own,c.rank);
                var xml:XML=<Members/>;xml.@name=c.guild;xml.appendChild(<CurrentFame>{c.fame}</CurrentFame>);xml.appendChild(<GuildLevel>1</GuildLevel>);
                for each(var member:Object in c.members)xml.appendChild(<Member><Name>{member.name}</Name><Rank>{member.rank}</Rank><Fame>{member.fame}</Fame></Member>);
                if(!c.loading)client.complete.dispatch(true,xml.toXMLString());
                var screen:Sprite=new Sprite();var frame:MenuFrame=new MenuFrame();screen.addChild(frame);screen.addChild(list);
                var button:TitleMenuOption=new TitleMenuOption("continue",36,false);button.x=400-button.width/2;button.y=520;screen.addChild(button);
                if(c.hover)button.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER));
                for(var i:int=0;i<list.numChildren;++i)if(list.getChildAt(i) is Scrollbar)Scrollbar(list.getChildAt(i)).setPos(c.scroll||0);
                addChild(screen);
                if(c.hasOwnProperty("action")) {
                    var main:Sprite=list.getChildAt(4) as Sprite;var rows:Sprite=main.getChildAt(1) as Sprite;var line:Sprite=rows.getChildAt(c.row||0) as Sprite;
                    var wanted:Number=c.action==0 ? 676 : c.action==1 ? 706 : 730;
                    for(var j:int=0;j<line.numChildren;++j) { var child:DisplayObject=line.getChildAt(j);if(child.x==wanted)child.dispatchEvent(new MouseEvent(MouseEvent.CLICK)); }
                }
                if(c.hasOwnProperty("hover_action")) {
                    var hm:Sprite=list.getChildAt(4) as Sprite;var hr:Sprite=hm.getChildAt(1) as Sprite;var hl:Sprite=hr.getChildAt(c.row||0) as Sprite;
                    for(var hi:int=0;hi<hl.numChildren;++hi) { var hc:DisplayObject=hl.getChildAt(hi);if(hc.x==676)hc.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER)); }
                }
                var s:Number=c.height/600;var image:BitmapData=new BitmapData(c.width,c.height,false,0x363636);
                var dim:Sprite=new Sprite();dim.graphics.beginFill(0x2b2b2b,.8);dim.graphics.drawRect(0,0,c.width,c.height);image.draw(dim);
                // MenuFrame is window-bound; source vector content stays at design size.
                var chrome:MenuFrame=new MenuFrame();screen.removeChild(frame);image.draw(chrome,new Matrix(c.width/800,0,0,s));
                image.draw(screen,new Matrix(s,0,0,s,(c.width-800*s)/2,0));
                if(c.hasOwnProperty("action")) { var dialog:Dialog=stage.getChildAt(stage.numChildren-1) as Dialog;image.draw(dialog.box_,new Matrix(s,0,0,s,(c.width-dialog.box_.width*s)/2,(c.height-dialog.box_.height*s)/2));stage.removeChild(dialog); }
                save(new File(tasks.output+"/"+c.key+".png"),image);removeChild(screen);
                var sourceMain:Sprite=c.loading ? null : list.getChildAt(4) as Sprite;metrics.push({key:c.key,members:c.loading ? 0 : c.members.length,height:c.loading ? 0 : sourceMain.getChildAt(1).height});
            }
            stream.open(new File(tasks.output+"/metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics,null,2));stream.close();
        }catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1);return; }
        NativeApplication.nativeApplication.exit(0);
    }
}
}
import kabam.rotmg.account.core.Account;
import kabam.rotmg.appengine.api.AppEngineClient;
import org.osflash.signals.OnceSignal;
class FixtureAccount implements Account {
public function updateUser(g:String,p:String):void {} public function getUserName():String { return "Fixture"; } public function getUsername():String { return "Fixture"; } public function getPassword():String { return ""; } public function getCredentials():Object { return {}; } public function isRegistered():Boolean { return true; } public function clear():void {} public function reportIntStat(n:String,v:int):void {}
}
class FixtureClient implements AppEngineClient {
private var signal:OnceSignal=new OnceSignal(Boolean,String);public function get complete():OnceSignal { return signal; }
public function setDataFormat(v:String):void {} public function setSendEncrypted(v:Boolean):void {} public function setMaxRetries(v:int):void {} public function sendRequest(p:String,v:Object):void {}
}
