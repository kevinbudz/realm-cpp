package {
import com.company.assembleegameclient.util.ReferenceAssets;
import com.company.assembleegameclient.util.AnimatedChar;
import com.company.assembleegameclient.util.AnimatedChars;
import com.company.assembleegameclient.util.MaskedImage;
import com.company.assembleegameclient.util.TextureRedrawer;
import com.company.assembleegameclient.util.redrawers.GlowRedrawer;
import com.company.assembleegameclient.util.FameUtil;
import com.company.assembleegameclient.objects.ObjectLibrary;
import com.company.assembleegameclient.parameters.Parameters;
import com.company.assembleegameclient.ui.TextButton;
import com.company.assembleegameclient.ui.dialogs.Dialog;
import com.company.assembleegameclient.ui.dialogs.ErrorDialog;
import kabam.rotmg.ui.view.NotEnoughGoldDialog;
import kabam.rotmg.game.view.CreditDisplay;
import com.company.util.AssetLibrary;
import com.company.ui.SimpleText;
import kabam.rotmg.assets.EmbeddedData;
import kabam.rotmg.assets.model.CharacterTemplate;
import kabam.rotmg.assets.model.Animation;
import kabam.rotmg.assets.services.CharacterFactory;
import kabam.rotmg.classes.model.CharacterSkin;
import kabam.rotmg.classes.model.CharacterSkinState;
import kabam.rotmg.classes.model.CharacterSkins;
import kabam.rotmg.classes.view.CharacterSkinListItemFactory;
import kabam.rotmg.classes.view.CharacterSkinListView;
import kabam.rotmg.classes.view.ClassDetailView;
import kabam.rotmg.characters.reskin.view.ReskinCharacterView;
import kabam.rotmg.util.components.StarsView;
import flash.display.Sprite;
import flash.display.Bitmap;
import flash.display.BitmapData;
import flash.display.DisplayObject;
import flash.display.PNGEncoderOptions;
import flash.geom.Matrix;
import flash.events.InvokeEvent;
import flash.events.MouseEvent;
import flash.desktop.NativeApplication;
import flash.filesystem.File;
import flash.filesystem.FileStream;
import flash.filesystem.FileMode;
public class SkinsExporter extends Sprite {
 private var stream:FileStream=new FileStream();private var entries:Array=[];private var art:File;private var factory:CharacterFactory=new CharacterFactory();
 public function SkinsExporter() { NativeApplication.nativeApplication.addEventListener(InvokeEvent.INVOKE,run); }
 private function save(file:File,im:BitmapData):void { file.parent.createDirectory();stream.open(file,FileMode.WRITE);stream.writeBytes(im.encode(im.rect,new PNGEncoderOptions()));stream.close();im.dispose(); }
 private function record(key:String,im:BitmapData):void { entries.push(key);save(art.resolvePath(key+".png"),im); }
 private function template(xml:XML):CharacterTemplate { return new CharacterTemplate(String(xml.AnimatedTexture.File),int(xml.AnimatedTexture.Index)); }
 private function bake(type:int,id:int,t:CharacterTemplate):void {
  var character:AnimatedChar=factory.makeCharacter(t);var frame:MaskedImage=character.imageFromDir(AnimatedChar.RIGHT,AnimatedChar.STAND,0);
  if(id) { var p:BitmapData=GlowRedrawer.outlineGlow(TextureRedrawer.resize(frame.image_,frame.mask_,4/frame.image_.width*100,true,0,0),0,0);var box:BitmapData=new BitmapData(80,80,true,0);box.draw(p);record("portrait"+id,box); }
  record("skinicon"+type+"-"+id,factory.makeIcon(t));
  for(var i:int=0;i<2;++i) { var f:MaskedImage=character.imageFromDir(AnimatedChar.RIGHT,AnimatedChar.WALK,i==0 ? .5 : 0);record("skinwalk"+type+"-"+id+"-"+i,GlowRedrawer.outlineGlow(TextureRedrawer.resize(f.image_,f.mask_,200,false,0,0),0)); }
 }
 private function skins(c:Object):CharacterSkins {
  var result:CharacterSkins=new CharacterSkins();var xml:XML=ObjectLibrary.xmlLibrary_[c.type];var skin:CharacterSkin=new CharacterSkin();skin.name="Classic";skin.template=template(xml);skin.setState(CharacterSkinState.OWNED);skin.setIsSelected(true);result.addSkin(skin,true);
  for each(xml in EmbeddedData.skinsXML.children())if(int(xml.PlayerClassType)==c.type) { skin=new CharacterSkin();skin.id=int(xml.@type);skin.name=String(xml.@id);skin.template=template(xml);skin.setState(c.owned.indexOf(skin.id)>=0 ? CharacterSkinState.OWNED : CharacterSkinState.PURCHASABLE);skin.setIsSelected(false);result.addSkin(skin); }
  result.getSkin(c.selected).setIsSelected(true);return result;
 }
 private function run(event:InvokeEvent):void {
  try {
   Parameters.data_={};Parameters.setDefaults();Parameters.GPURenderError=true;new ReferenceAssets().load();new SimpleText(18,0xffffff);
   stream.open(new File(event.arguments[0]),FileMode.READ);var tasks:Object=JSON.parse(stream.readUTFBytes(stream.bytesAvailable));stream.close();
   if(tasks.art) {
    art=new File(tasks.art);art.createDirectory();var xml:XML;
    for each(var playerXML:XML in ObjectLibrary.playerChars_)bake(int(playerXML.@type),0,template(playerXML));
    for each(xml in EmbeddedData.skinsXML.children())bake(int(xml.PlayerClassType),int(xml.@type),template(xml));
    for(var n:int=0;n<=5;++n) { var stars:StarsView=new StarsView();stars.setStars(n);var im:BitmapData=new BitmapData(Math.ceil(stars.width),Math.ceil(stars.height),true,0);im.draw(stars);record("skinstars"+n,im); }
    record("skinfame",FameUtil.getFameIcon().clone());record("skincreditcoin",TextureRedrawer.redraw(AssetLibrary.getImageFromSet("lofiObj3",225),40,true,0));
    stream.open(art.resolvePath("entries.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(entries));stream.close();
   }
   var output:File=new File(tasks.output);output.createDirectory();var metrics:Array=[];
   for each(var c:Object in tasks.cases) {
    var models:CharacterSkins=skins(c);var tiles:CharacterSkinListItemFactory=new CharacterSkinListItemFactory();tiles.characters=factory;
    var screen:Sprite=new Sprite();var view:ReskinCharacterView;var height:Number=600;
    if(!c.startup) { view=new ReskinCharacterView();screen.addChild(view);addChild(screen);var items:Vector.<DisplayObject>=tiles.make(models);view.setList(items);height=view.viewHeight;
     if(c.hasOwnProperty("hover_row"))items[c.hover_row].dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER));
     if(c.hasOwnProperty("hover_button"))for(var childIndex:int=0;childIndex<view.numChildren;++childIndex) { var child:DisplayObject=view.getChildAt(childIndex);if(child is TextButton&&((c.hover_button==0&&child.x<200)||(c.hover_button==1&&child.x>200)))child.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_OVER)); }view.x=0;view.y=0;view.scaleX=view.scaleY=1; }
    else { var list:CharacterSkinListView=new CharacterSkinListView();screen.addChild(list);list.x=351;list.y=110;list.setItems(tiles.make(models));var detail:ClassDetailView=new ClassDetailView();screen.addChild(detail);detail.x=5;detail.y=110;xml=ObjectLibrary.xmlLibrary_[c.type];detail.setData(String(xml.DisplayId.length()?xml.DisplayId:xml.@id),String(xml.Description),FameUtil.numStars(c.fame||0),c.level||0,c.fame||0);detail.setNextGoal(String(xml.@id),FameUtil.nextStarFame(c.fame||0,0));var walk:Animation=factory.makeWalkingIcon(models.getSkin(c.selected).template,200);detail.setWalkingAnimation(walk);walk.stop();addChild(screen); }
    var s:Number=c.height/600;im=new BitmapData(c.width,c.height,false,0x363636);
    if(!c.startup) { var dim:Sprite=new Sprite();dim.graphics.beginFill(0x151515,.6);dim.graphics.drawRect(0,0,c.width,c.height);im.draw(dim); }
    if(c.startup||(!c.error&&!c.insufficient))im.draw(screen,new Matrix(s,0,0,s,(c.width-(c.startup?800:462)*s)/2,c.startup?0:(c.height-height*s)/2));if(c.startup) { var credits:CreditDisplay=new CreditDisplay();credits.draw(c.credits||0,c.accountFame||0);im.draw(credits,new Matrix(s,0,0,s,c.width,20*s)); }if(c.error||c.insufficient) {
     if(c.startup) { dim=new Sprite();dim.graphics.beginFill(0x151515,.6);dim.graphics.drawRect(0,0,c.width,c.height);im.draw(dim); }
     var dialog:Dialog=c.insufficient ? new NotEnoughGoldDialog() : new ErrorDialog(c.error);addChild(dialog);
     im.draw(dialog.box_,new Matrix(s,0,0,s,(c.width-dialog.box_.width*s)/2,(c.height-dialog.box_.height*s)/2));removeChild(dialog);
    }save(output.resolvePath(c.key+".png"),im);metrics.push({key:c.key,height:height});removeChild(screen);
   }
   stream.open(output.resolvePath("metrics.json"),FileMode.WRITE);stream.writeUTFBytes(JSON.stringify(metrics));stream.close();
  } catch(error:Error) { trace(error.getStackTrace());NativeApplication.nativeApplication.exit(1);return; }
  NativeApplication.nativeApplication.exit(0);
 }
}
}
