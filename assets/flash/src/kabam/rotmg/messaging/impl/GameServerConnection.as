package kabam.rotmg.messaging.impl
{
import com.adobe.crypto.SHA1;
import com.company.assembleegameclient.game.GameSprite;
   import com.company.assembleegameclient.game.events.GuildResultEvent;
   import com.company.assembleegameclient.game.events.NameResultEvent;
   import com.company.assembleegameclient.game.events.ReconnectEvent;
   import com.company.assembleegameclient.map.GroundLibrary;
   import com.company.assembleegameclient.map.Map;
import com.company.assembleegameclient.map.mapoverlay.CharacterStatusText;
import com.company.assembleegameclient.objects.Character;
import com.company.assembleegameclient.objects.Container;
   import com.company.assembleegameclient.objects.FlashDescription;
   import com.company.assembleegameclient.objects.GameObject;
   import com.company.assembleegameclient.objects.Merchant;
   import com.company.assembleegameclient.objects.ObjectLibrary;
   import com.company.assembleegameclient.objects.Player;
   import com.company.assembleegameclient.objects.Portal;
   import com.company.assembleegameclient.objects.Projectile;
   import com.company.assembleegameclient.objects.SellableObject;
   import com.company.assembleegameclient.objects.particles.AOEEffect;
   import com.company.assembleegameclient.objects.particles.BurstEffect;
   import com.company.assembleegameclient.objects.particles.CollapseEffect;
   import com.company.assembleegameclient.objects.particles.ConeBlastEffect;
   import com.company.assembleegameclient.objects.particles.FlowEffect;
   import com.company.assembleegameclient.objects.particles.HealEffect;
   import com.company.assembleegameclient.objects.particles.LightningEffect;
   import com.company.assembleegameclient.objects.particles.LineEffect;
   import com.company.assembleegameclient.objects.particles.NovaEffect;
   import com.company.assembleegameclient.objects.particles.ParticleEffect;
   import com.company.assembleegameclient.objects.particles.PoisonEffect;
   import com.company.assembleegameclient.objects.particles.RingEffect;
   import com.company.assembleegameclient.objects.particles.StreamEffect;
   import com.company.assembleegameclient.objects.particles.TeleportEffect;
   import com.company.assembleegameclient.objects.particles.ThrowEffect;
   import com.company.assembleegameclient.objects.thrown.ThrowProjectileEffect;
   import com.company.assembleegameclient.parameters.Parameters;
   import com.company.assembleegameclient.sound.SoundEffectLibrary;
   import com.company.assembleegameclient.ui.dialogs.Dialog;
   import com.company.assembleegameclient.ui.dialogs.ErrorDialog;
   import com.company.assembleegameclient.ui.dialogs.NotEnoughFameDialog;
   import com.company.assembleegameclient.ui.panels.GuildInvitePanel;
   import com.company.assembleegameclient.ui.panels.TradeRequestPanel;
   import com.company.assembleegameclient.util.Currency;
   import com.company.assembleegameclient.util.FreeList;
   import com.company.util.MoreStringUtil;
   import com.company.util.Random;
import com.company.util.Trig;
import com.hurlant.crypto.Crypto;
   import com.hurlant.crypto.rsa.RSAKey;
   import com.hurlant.crypto.symmetric.ICipher;
   import com.hurlant.util.Base64;
   import com.hurlant.util.der.PEM;
   import flash.display.BitmapData;
   import flash.events.Event;
   import flash.events.TimerEvent;
   import flash.geom.Point;
   import flash.net.FileReference;
   import flash.utils.ByteArray;
   import flash.utils.Timer;
   import flash.utils.getTimer;
   import kabam.lib.net.api.MessageMap;
   import kabam.lib.net.api.MessageProvider;
   import kabam.lib.net.impl.Message;
   import kabam.lib.net.impl.SocketServer;
   import kabam.rotmg.account.core.Account;
   import kabam.rotmg.classes.model.CharacterClass;
import kabam.rotmg.classes.model.CharacterSkin;
import kabam.rotmg.classes.model.CharacterSkinState;
import kabam.rotmg.classes.model.ClassesModel;
   import kabam.rotmg.constants.GeneralConstants;
   import kabam.rotmg.constants.ItemConstants;
   import kabam.rotmg.core.StaticInjectorContext;
   import kabam.rotmg.core.model.PlayerModel;
   import kabam.rotmg.death.control.HandleDeathSignal;
   import kabam.rotmg.dialogs.control.OpenDialogSignal;
   import kabam.rotmg.game.focus.control.SetGameFocusSignal;
   import kabam.rotmg.game.model.AddSpeechBalloonVO;
   import kabam.rotmg.game.model.AddTextLineVO;
   import kabam.rotmg.game.model.GameModel;
import kabam.rotmg.game.model.PotionInventoryModel;
import kabam.rotmg.game.signals.AddSpeechBalloonSignal;
   import kabam.rotmg.game.signals.AddTextLineSignal;
import kabam.rotmg.messaging.impl.data.GroundTileData;
   import kabam.rotmg.messaging.impl.data.ObjectData;
import kabam.rotmg.messaging.impl.data.ObjectDropData;
import kabam.rotmg.messaging.impl.data.ObjectStatusData;
   import kabam.rotmg.messaging.impl.data.StatData;
   import kabam.rotmg.messaging.impl.incoming.AccountList;
   import kabam.rotmg.messaging.impl.incoming.AllyShoot;
   import kabam.rotmg.messaging.impl.incoming.Aoe;
   import kabam.rotmg.messaging.impl.incoming.BuyResult;
   import kabam.rotmg.messaging.impl.incoming.CreateSuccess;
   import kabam.rotmg.messaging.impl.incoming.Damage;
   import kabam.rotmg.messaging.impl.incoming.Death;
   import kabam.rotmg.messaging.impl.incoming.EnemyShoot;
   import kabam.rotmg.messaging.impl.incoming.Failure;
import kabam.rotmg.messaging.impl.incoming.Goto;
import kabam.rotmg.messaging.impl.incoming.GuildResult;
   import kabam.rotmg.messaging.impl.incoming.InvResult;
   import kabam.rotmg.messaging.impl.incoming.InvitedToGuild;
   import kabam.rotmg.messaging.impl.incoming.MapInfo;
   import kabam.rotmg.messaging.impl.incoming.GlobalNotification;
   import kabam.rotmg.messaging.impl.incoming.NameResult;
   import kabam.rotmg.messaging.impl.incoming.NewTick;
   import kabam.rotmg.messaging.impl.incoming.Notification;
   import kabam.rotmg.messaging.impl.incoming.Ping;
   import kabam.rotmg.messaging.impl.incoming.PlaySound;
   import kabam.rotmg.messaging.impl.incoming.QuestObjId;
   import kabam.rotmg.messaging.impl.incoming.Reconnect;
import kabam.rotmg.messaging.impl.incoming.ServerPlayerShoot;
   import kabam.rotmg.messaging.impl.incoming.ShowEffect;
   import kabam.rotmg.messaging.impl.incoming.Text;
   import kabam.rotmg.messaging.impl.incoming.TradeAccepted;
   import kabam.rotmg.messaging.impl.incoming.TradeChanged;
   import kabam.rotmg.messaging.impl.incoming.TradeDone;
   import kabam.rotmg.messaging.impl.incoming.TradeRequested;
   import kabam.rotmg.messaging.impl.incoming.TradeStart;
   import kabam.rotmg.messaging.impl.incoming.Update;
   import kabam.rotmg.messaging.impl.outgoing.AcceptTrade;
   import kabam.rotmg.messaging.impl.outgoing.AoeAck;
   import kabam.rotmg.messaging.impl.outgoing.Buy;
   import kabam.rotmg.messaging.impl.outgoing.CancelTrade;
   import kabam.rotmg.messaging.impl.outgoing.ChangeGuildRank;
   import kabam.rotmg.messaging.impl.outgoing.ChangeTrade;
   import kabam.rotmg.messaging.impl.outgoing.ChooseName;
   import kabam.rotmg.messaging.impl.outgoing.Create;
   import kabam.rotmg.messaging.impl.outgoing.CreateGuild;
   import kabam.rotmg.messaging.impl.outgoing.EditAccountList;
   import kabam.rotmg.messaging.impl.outgoing.EnemyHit;
   import kabam.rotmg.messaging.impl.outgoing.Escape;
import kabam.rotmg.messaging.impl.outgoing.GotoAck;
import kabam.rotmg.messaging.impl.outgoing.GuildInvite;
   import kabam.rotmg.messaging.impl.outgoing.GuildRemove;
   import kabam.rotmg.messaging.impl.outgoing.Hello;
   import kabam.rotmg.messaging.impl.outgoing.InvDrop;
   import kabam.rotmg.messaging.impl.outgoing.InvSwap;
   import kabam.rotmg.messaging.impl.outgoing.JoinGuild;
   import kabam.rotmg.messaging.impl.outgoing.Load;
   import kabam.rotmg.messaging.impl.outgoing.Move;
   import kabam.rotmg.messaging.impl.outgoing.PlayerHit;
   import kabam.rotmg.messaging.impl.outgoing.PlayerShoot;
   import kabam.rotmg.messaging.impl.outgoing.PlayerText;
   import kabam.rotmg.messaging.impl.outgoing.Pong;
   import kabam.rotmg.messaging.impl.outgoing.RequestTrade;
   import kabam.rotmg.messaging.impl.outgoing.Reskin;
   import kabam.rotmg.messaging.impl.outgoing.ShootAck;
   import kabam.rotmg.messaging.impl.outgoing.SquareHit;
   import kabam.rotmg.messaging.impl.outgoing.Teleport;
   import kabam.rotmg.messaging.impl.outgoing.UseItem;
   import kabam.rotmg.messaging.impl.outgoing.UsePortal;
   import kabam.rotmg.minimap.control.UpdateGameObjectTileSignal;
   import kabam.rotmg.minimap.control.UpdateGroundTileSignal;
   import kabam.rotmg.minimap.model.UpdateGroundTileVO;
import kabam.rotmg.stage3D.Renderer;
import kabam.rotmg.ui.model.Key;
import kabam.rotmg.ui.model.UpdateGameObjectTileVO;
import kabam.rotmg.ui.signals.ShowKeySignal;
import kabam.rotmg.ui.signals.ShowKeyUISignal;
import kabam.rotmg.ui.signals.UpdateBackpackTabSignal;
import kabam.rotmg.ui.signals.UpdateHUDSignal;
import kabam.rotmg.ui.view.MessageCloseDialog;
import kabam.rotmg.ui.view.NotEnoughGoldDialog;
   import org.swiftsuspenders.Injector;
   import robotlegs.bender.framework.api.ILogger;
   
   public class GameServerConnection
   {
      public static const FAILURE:int = 0;
      public static const CREATE_SUCCESS:int = 1;
      public static const CREATE:int = 2;
      public static const PLAYERSHOOT:int = 3;
      public static const MOVE:int = 4;
      public static const PLAYERTEXT:int = 5;
      public static const TEXT:int = 6;
      public static const SERVERPLAYERSHOOT:int = 7;
      public static const DAMAGE:int = 8;
      public static const UPDATE:int = 9;
      public static const NOTIFICATION:int = 10;
      public static const NEWTICK:int = 11;
      public static const INVSWAP:int = 12;
      public static const USEITEM:int = 13;
      public static const SHOWEFFECT:int = 14;
      public static const HELLO:int = 15;
      public static const GOTO:int = 16;
      public static const INVDROP:int = 17;
      public static const INVRESULT:int = 18;
      public static const RECONNECT:int = 19;
      public static const MAPINFO:int = 20;
      public static const LOAD:int = 21;
      public static const TELEPORT:int = 22;
      public static const USEPORTAL:int = 23;
      public static const DEATH:int = 24;
      public static const BUY:int = 25;
      public static const BUYRESULT:int = 26;
      public static const AOE:int = 27;
      public static const PLAYERHIT:int = 28;
      public static const ENEMYHIT:int = 29;
      public static const AOEACK:int = 30;
      public static const SHOOTACK:int = 31;
      public static const SQUAREHIT:int = 32;
      public static const EDITACCOUNTLIST:int = 33;
      public static const ACCOUNTLIST:int = 34;
      public static const QUESTOBJID:int = 35;
      public static const CREATEGUILD:int = 36;
      public static const GUILDRESULT:int = 37;
      public static const GUILDREMOVE:int = 38;
      public static const GUILDINVITE:int = 39;
      public static const ALLYSHOOT:int = 40;
      public static const ENEMYSHOOT:int = 41;
      public static const ESCAPE:int = 42;
      public static const INVITEDTOGUILD:int = 43;
      public static const JOINGUILD:int = 44;
      public static const CHANGEGUILDRANK:int = 45;
      public static const PLAYSOUND:int = 46;
      public static const RESKIN:int = 47;
      public static const GOTOACK:int = 48;
      public static const CHOOSENAME:int = 49;
      public static const NAMERESULT:int = 50;
      public static const REQUESTTRADE:int = 51;
      public static const TRADEREQUESTED:int = 52;
      public static const TRADESTART:int = 53;
      public static const CHANGETRADE:int = 54;
      public static const TRADECHANGED:int = 55;
      public static const ACCEPTTRADE:int = 56;
      public static const CANCELTRADE:int = 57;
      public static const TRADEDONE:int = 58;
      public static const TRADEACCEPTED:int = 59;
      public static const GLOBAL_NOTIFICATION:int = 60;
      public static const PING:int = 61;
      public static const PONG:int = 62;

      public static var instance:GameServerConnection;

      private static const NORMAL_SPEECH_COLORS:Vector.<uint> = new <uint>[14802908,16777215,5526612];
      private static const ENEMY_SPEECH_COLORS:Vector.<uint> = new <uint>[5644060,16549442,13484223];
      private static const TELL_SPEECH_COLORS:Vector.<uint> = new <uint>[2493110,61695,13880567];
      private static const GUILD_SPEECH_COLORS:Vector.<uint> = new <uint>[4098560,10944349,13891532];

      public var gs_:GameSprite;
      public var gameId_:int;
      public var createCharacter_:Boolean;
      public var charId_:int;
      public var mapJSON_:String;
      public var jitterWatcher_:JitterWatcher = null;
      public var serverConnection:SocketServer;
      private var messages:MessageProvider;
      private var playerId_:int = -1;
      private var player:Player;
      public var outstandingBuy_:OutstandingBuy = null;
      private var pendingInvOps_:Array = [];
      private var updateHUD:UpdateHUDSignal;
      private var rand_:Random = null;
      private var death:Death;
      private var addTextLine:AddTextLineSignal;
      private var addSpeechBalloon:AddSpeechBalloonSignal;
      private var updateGroundTileSignal:UpdateGroundTileSignal;
      private var updateGameObjectTileSignal:UpdateGameObjectTileSignal;
      private var logger:ILogger;
      private var handleDeath:HandleDeathSignal;
      private var setGameFocus:SetGameFocusSignal;
      private var updateBackpackTab:UpdateBackpackTabSignal;
      private var classesModel:ClassesModel;
      private var playerModel:PlayerModel;
      private var injector:Injector;
      private var model:GameModel;

      
      public function GameServerConnection(gs:GameSprite, gameId:int, createCharacter:Boolean, charId:int, mapJSON:String)
      {
         super();
         this.injector = StaticInjectorContext.getInjector();
         this.addTextLine = this.injector.getInstance(AddTextLineSignal);
         this.addSpeechBalloon = this.injector.getInstance(AddSpeechBalloonSignal);
         this.updateGroundTileSignal = this.injector.getInstance(UpdateGroundTileSignal);
         this.updateGameObjectTileSignal = this.injector.getInstance(UpdateGameObjectTileSignal);
         this.updateBackpackTab = StaticInjectorContext.getInjector().getInstance(UpdateBackpackTabSignal);
         this.logger = this.injector.getInstance(ILogger);
         this.handleDeath = this.injector.getInstance(HandleDeathSignal);
         this.setGameFocus = this.injector.getInstance(SetGameFocusSignal);
         this.classesModel = this.injector.getInstance(ClassesModel);
         this.serverConnection = this.injector.getInstance(SocketServer);
         this.messages = this.injector.getInstance(MessageProvider);
         this.model = this.injector.getInstance(GameModel);
         this.playerModel = this.injector.getInstance(PlayerModel);
         this.updateHUD = this.injector.getInstance(UpdateHUDSignal);
         instance = this;
         this.gs_ = gs;
         this.gameId_ = gameId;
         this.createCharacter_ = createCharacter;
         this.charId_ = charId;
         this.mapJSON_ = mapJSON;
      }
      
      public function disconnect() : void
      {
         this.pendingInvOps_.length = 0;
         this.removeServerConnectionListeners();
         this.serverConnection.disconnect();
         this.unmapMessages();
      }
      
      private function removeServerConnectionListeners() : void
      {
         this.serverConnection.connected.remove(this.onConnected);
         this.serverConnection.closed.remove(this.onClosed);
         this.serverConnection.error.remove(this.onError);
      }
      
      public function connect() : void
      {
         this.addServerConnectionListeners();
         this.mapMessages();
         this.addTextLine.dispatch(new AddTextLineVO(Parameters.CLIENT_CHAT_NAME,"Connecting..."));
         this.serverConnection.connect(Parameters.SERVER_ADDRESS,Parameters.GAME_PORT);
      }
      
      private function addServerConnectionListeners() : void
      {
         this.serverConnection.connected.add(this.onConnected);
         this.serverConnection.closed.add(this.onClosed);
         this.serverConnection.error.add(this.onError);
      }
      
      private function mapMessages() : void
      {
         var messages:MessageMap = this.injector.getInstance(MessageMap);
         messages.map(CREATE).toMessage(Create);
         messages.map(PLAYERSHOOT).toMessage(PlayerShoot);
         messages.map(MOVE).toMessage(Move);
         messages.map(PLAYERTEXT).toMessage(PlayerText);
         messages.map(INVSWAP).toMessage(InvSwap);
         messages.map(USEITEM).toMessage(UseItem);
         messages.map(HELLO).toMessage(Hello);
         messages.map(INVDROP).toMessage(InvDrop);
         messages.map(LOAD).toMessage(Load);
         messages.map(TELEPORT).toMessage(Teleport);
         messages.map(USEPORTAL).toMessage(UsePortal);
         messages.map(BUY).toMessage(Buy);
         messages.map(PLAYERHIT).toMessage(PlayerHit);
         messages.map(ENEMYHIT).toMessage(EnemyHit);
         messages.map(AOEACK).toMessage(AoeAck);
         messages.map(SHOOTACK).toMessage(ShootAck);
         messages.map(SQUAREHIT).toMessage(SquareHit);
         messages.map(CREATEGUILD).toMessage(CreateGuild);
         messages.map(GUILDREMOVE).toMessage(GuildRemove);
         messages.map(GUILDINVITE).toMessage(GuildInvite);
         messages.map(ESCAPE).toMessage(Escape);
         messages.map(JOINGUILD).toMessage(JoinGuild);
         messages.map(CHANGEGUILDRANK).toMessage(ChangeGuildRank);
         messages.map(EDITACCOUNTLIST).toMessage(EditAccountList);
         messages.map(FAILURE).toMessage(Failure).toMethod(this.onFailure);
         messages.map(CREATE_SUCCESS).toMessage(CreateSuccess).toMethod(this.onCreateSuccess);
         messages.map(TEXT).toMessage(Text).toMethod(this.onText);
         messages.map(SERVERPLAYERSHOOT).toMessage(ServerPlayerShoot).toMethod(this.onServerPlayerShoot);
         messages.map(DAMAGE).toMessage(Damage).toMethod(this.onDamage);
         messages.map(UPDATE).toMessage(Update).toMethod(this.onUpdate);
         messages.map(NOTIFICATION).toMessage(Notification).toMethod(this.onNotification);
         messages.map(NEWTICK).toMessage(NewTick).toMethod(this.onNewTick);
         messages.map(SHOWEFFECT).toMessage(ShowEffect).toMethod(this.onShowEffect);
         messages.map(GOTO).toMessage(Goto).toMethod(this.onGoto);
         messages.map(GOTOACK).toMessage(GotoAck);
         messages.map(INVRESULT).toMessage(InvResult).toMethod(this.onInvResult);
         messages.map(RECONNECT).toMessage(Reconnect).toMethod(this.onReconnect);
         messages.map(MAPINFO).toMessage(MapInfo).toMethod(this.onMapInfo);
         messages.map(DEATH).toMessage(Death).toMethod(this.onDeath);
         messages.map(BUYRESULT).toMessage(BuyResult).toMethod(this.onBuyResult);
         messages.map(AOE).toMessage(Aoe).toMethod(this.onAoe);
         messages.map(ACCOUNTLIST).toMessage(AccountList).toMethod(this.onAccountList);
         messages.map(QUESTOBJID).toMessage(QuestObjId).toMethod(this.onQuestObjId);
         messages.map(GUILDRESULT).toMessage(GuildResult).toMethod(this.onGuildResult);
         messages.map(ALLYSHOOT).toMessage(AllyShoot).toMethod(this.onAllyShoot);
         messages.map(ENEMYSHOOT).toMessage(EnemyShoot).toMethod(this.onEnemyShoot);
         messages.map(INVITEDTOGUILD).toMessage(InvitedToGuild).toMethod(this.onInvitedToGuild);
         messages.map(PLAYSOUND).toMessage(PlaySound).toMethod(this.onPlaySound);
         messages.map(CHOOSENAME).toMessage(ChooseName);
         messages.map(REQUESTTRADE).toMessage(RequestTrade);
         messages.map(CHANGETRADE).toMessage(ChangeTrade);
         messages.map(ACCEPTTRADE).toMessage(AcceptTrade);
         messages.map(CANCELTRADE).toMessage(CancelTrade);
         messages.map(NAMERESULT).toMessage(NameResult).toMethod(this.onNameResult);
         messages.map(TRADEREQUESTED).toMessage(TradeRequested).toMethod(this.onTradeRequested);
         messages.map(TRADESTART).toMessage(TradeStart).toMethod(this.onTradeStart);
         messages.map(TRADECHANGED).toMessage(TradeChanged).toMethod(this.onTradeChanged);
         messages.map(TRADEDONE).toMessage(TradeDone).toMethod(this.onTradeDone);
         messages.map(TRADEACCEPTED).toMessage(TradeAccepted).toMethod(this.onTradeAccepted);
         messages.map(GLOBAL_NOTIFICATION).toMessage(GlobalNotification).toMethod(this.onGlobalNotification);
         messages.map(PING).toMessage(Ping).toMethod(this.onPing);
         messages.map(PONG).toMessage(Pong);
      }
      
      private function unmapMessages() : void
      {
         var messages:MessageMap = this.injector.getInstance(MessageMap);
         messages.unmap(CREATE);
         messages.unmap(PLAYERSHOOT);
         messages.unmap(MOVE);
         messages.unmap(PLAYERTEXT);
         messages.unmap(INVSWAP);
         messages.unmap(USEITEM);
         messages.unmap(HELLO);
         messages.unmap(INVDROP);
         messages.unmap(LOAD);
         messages.unmap(TELEPORT);
         messages.unmap(USEPORTAL);
         messages.unmap(BUY);
         messages.unmap(PLAYERHIT);
         messages.unmap(ENEMYHIT);
         messages.unmap(AOEACK);
         messages.unmap(SHOOTACK);
         messages.unmap(SQUAREHIT);
         messages.unmap(CREATEGUILD);
         messages.unmap(GUILDREMOVE);
         messages.unmap(GUILDINVITE);
         messages.unmap(ESCAPE);
         messages.unmap(JOINGUILD);
         messages.unmap(CHANGEGUILDRANK);
         messages.unmap(REQUESTTRADE);
         messages.unmap(CHANGETRADE);
         messages.unmap(ACCEPTTRADE);
         messages.unmap(CANCELTRADE);
         messages.unmap(EDITACCOUNTLIST);
         messages.unmap(FAILURE);
         messages.unmap(CREATE_SUCCESS);
         messages.unmap(TEXT);
         messages.unmap(SERVERPLAYERSHOOT);
         messages.unmap(DAMAGE);
         messages.unmap(UPDATE);
         messages.unmap(NOTIFICATION);
         messages.unmap(NEWTICK);
         messages.unmap(SHOWEFFECT);
         messages.unmap(GOTO);
         messages.unmap(INVRESULT);
         messages.unmap(RECONNECT);
         messages.unmap(MAPINFO);
         messages.unmap(DEATH);
         messages.unmap(BUYRESULT);
         messages.unmap(AOE);
         messages.unmap(ACCOUNTLIST);
         messages.unmap(QUESTOBJID);
         messages.unmap(GUILDRESULT);
         messages.unmap(ALLYSHOOT);
         messages.unmap(ENEMYSHOOT);
         messages.unmap(INVITEDTOGUILD);
         messages.unmap(TRADEREQUESTED);
         messages.unmap(TRADESTART);
         messages.unmap(TRADECHANGED);
         messages.unmap(TRADEDONE);
         messages.unmap(TRADEACCEPTED);
         messages.unmap(GLOBAL_NOTIFICATION);
         messages.unmap(PING);
         messages.unmap(PONG);
         messages.unmap(PLAYSOUND);
         messages.unmap(CHOOSENAME);
         messages.unmap(NAMERESULT);
      }
      
      public function getNextDamage(minDamage:uint, maxDamage:uint) : uint
      {
         return this.rand_.nextIntRange(minDamage,maxDamage);
      }
      
      public function enableJitterWatcher() : void
      {
         if(this.jitterWatcher_ == null)
         {
            this.jitterWatcher_ = new JitterWatcher();
         }
      }
      
      public function disableJitterWatcher() : void
      {
         if(this.jitterWatcher_ != null)
         {
            this.jitterWatcher_ = null;
         }
      }
      
      private function create() : void
      {
         var charClass:CharacterClass = this.classesModel.getSelected();
         var create:Create = this.messages.require(CREATE) as Create;
         create.classType = charClass.id;
         create.skinType = charClass.skins.getSelectedSkin().id;
         this.serverConnection.sendMessage(create);
      }
      
      private function load() : void
      {
         var load:Load = this.messages.require(LOAD) as Load;
         load.charId_ = this.charId_;
         this.serverConnection.sendMessage(load);
      }
      
      public function playerShoot(time:int, startX:Number, startY:Number, angle:Number, ability:Boolean, numShots:int) : void
      {
         var playerShoot:PlayerShoot = this.messages.require(PLAYERSHOOT) as PlayerShoot;
         playerShoot.time_ = time;
         playerShoot.startingPos_.x_ = startX;
         playerShoot.startingPos_.y_ = startY;
         playerShoot.angle_ = angle;
         playerShoot.ability_ = ability;
         playerShoot.numShots_ = numShots;
         this.serverConnection.sendMessage(playerShoot);
      }

      private function onGoto(gotoPkt:Goto) : void
      {
         if (gotoPkt.objectId_ == playerId_) {
            this.gs_.map.gotoRequested_++;
         }
         var go:GameObject = this.gs_.map.goDict_[gotoPkt.objectId_];
         if(go == null)
         {
            return;
         }
         go.onGoto(gotoPkt.pos_.x_, gotoPkt.pos_.y_, this.gs_.lastUpdate_);
      }

      private function onPing(ping:Ping) : void
      {
         if(this.gs_ == null || this.serverConnection == null)
         {
            return;
         }
         var pong:Pong = this.messages.require(PONG) as Pong;
         pong.serial_ = ping.serial_;
         pong.time_ = this.gs_.lastUpdate_;
         this.serverConnection.sendMessage(pong);
      }
      
      public function playerHit(bulletId:int) : void
      {
         var playerHit:PlayerHit = this.messages.require(PLAYERHIT) as PlayerHit;
         playerHit.bulletId_ = bulletId;
         this.serverConnection.sendMessage(playerHit);
      }
      
      public function enemyHit(time:int, bulletId:int, targetId:int) : void
      {
         var enemyHit:EnemyHit = this.messages.require(ENEMYHIT) as EnemyHit;
         enemyHit.time_ = time;
         enemyHit.bulletId_ = bulletId;
         enemyHit.targetId_ = targetId;
         this.serverConnection.sendMessage(enemyHit);
      }

      public function squareHit(time:int, bulletId:int) : void
      {
         var squareHit:SquareHit = this.messages.require(SQUAREHIT) as SquareHit;
         squareHit.time_ = time;
         squareHit.bulletId_ = bulletId;
         this.serverConnection.sendMessage(squareHit);
      }
      
      public function aoeAck(time:int, x:Number, y:Number) : void
      {
         var aoeAck:AoeAck = this.messages.require(AOEACK) as AoeAck;
         aoeAck.time_ = time;
         aoeAck.position_.x_ = x;
         aoeAck.position_.y_ = y;
         this.serverConnection.sendMessage(aoeAck);
      }
      
      public function shootAck(time:int) : void
      {
         var shootAck:ShootAck = this.messages.require(SHOOTACK) as ShootAck;
         shootAck.time_ = time;
         this.serverConnection.sendMessage(shootAck);
      }
      
      public function playerText(textStr:String) : void
      {
         var playerTextMessage:PlayerText = this.messages.require(PLAYERTEXT) as PlayerText;
         playerTextMessage.text_ = textStr;
         this.serverConnection.sendMessage(playerTextMessage);
      }


      public function invSwap(player:Player, sourceObj:GameObject, slotId1:int, targetObj:GameObject, slotId2:int) : Boolean
      {
         if(!this.gs_)
         {
            return false;
         }
         var prevItem1:int = this.readSlotItem(sourceObj,slotId1);
         var prevData1:int = this.readSlotData(sourceObj,slotId1);
         var prevItem2:int = this.readSlotItem(targetObj,slotId2);
         var prevData2:int = this.readSlotData(targetObj,slotId2);
         this.enqueueInvOp(sourceObj != null ? sourceObj.objectId_ : -1,slotId1,prevItem1,prevData1,targetObj != null ? targetObj.objectId_ : -1,slotId2,prevItem2,prevData2);
         var invSwap:InvSwap = this.messages.require(INVSWAP) as InvSwap;
         invSwap.time_ = this.gs_.lastUpdate_;
         invSwap.position_.x_ = player.x_;
         invSwap.position_.y_ = player.y_;
         invSwap.slotObject1_.objectId_ = sourceObj.objectId_;
         invSwap.slotObject1_.slotId_ = slotId1;
         invSwap.slotObject2_.objectId_ = targetObj.objectId_;
         invSwap.slotObject2_.slotId_ = slotId2;
         this.serverConnection.sendMessage(invSwap);

         var tempItem:int = sourceObj.equipment_[slotId1];
         var tempData:int = sourceObj.itemDatas_[slotId1];
         sourceObj.equipment_[slotId1] = targetObj.equipment_[slotId2];
         sourceObj.itemDatas_[slotId1] = targetObj.itemDatas_[slotId2];
         targetObj.equipment_[slotId2] = tempItem;
         targetObj.itemDatas_[slotId2] = tempData;

         SoundEffectLibrary.play("inventory_move_item");
         return true;
      }

      /*public function invSwapPotion(player:Player, sourceObj:GameObject, slotId1:int, itemId:int, targetObj:GameObject, slotId2:int, objectType2:int) : Boolean
      {
         if(!this.gs_)
         {
            return false;
         }
         var invSwap:InvSwap = this.messages.require(INVSWAP) as InvSwap;
         invSwap.time_ = this.gs_.lastUpdate_;
         invSwap.position_.x_ = player.x_;
         invSwap.position_.y_ = player.y_;
         invSwap.slotObject1_.objectId_ = sourceObj.objectId_;
         invSwap.slotObject1_.slotId_ = slotId1;
         invSwap.slotObject1_.objectType_ = itemId;
         invSwap.slotObject2_.objectId_ = targetObj.objectId_;
         invSwap.slotObject2_.slotId_ = slotId2;
         invSwap.slotObject2_.objectType_ = objectType2;
         sourceObj.equipment_[slotId1] = ItemConstants.NO_ITEM;
         if(itemId == PotionInventoryModel.HEALTH_POTION_ID)
         {
            player.healthPotionCount_++;
         }
         else if(itemId == PotionInventoryModel.MAGIC_POTION_ID)
         {
            player.magicPotionCount_++;
         }
         this.serverConnection.sendMessage(invSwap);
         SoundEffectLibrary.play("inventory_move_item");
         return true;
      }*/

      public function invDrop(object:GameObject, slotId:int) : void
      {
         var prevItem:int = this.readSlotItem(object,slotId);
         var prevData:int = this.readSlotData(object,slotId);
         this.enqueueInvOp(object != null ? object.objectId_ : -1,slotId,prevItem,prevData);
         var invDrop:InvDrop = this.messages.require(INVDROP) as InvDrop;
         invDrop.slotId_ = slotId;
         this.serverConnection.sendMessage(invDrop);
         if(object != null && object.equipment_ != null && slotId >= 0 && slotId < object.equipment_.length)
         {
            object.equipment_[slotId] = -1;
            if(object.itemDatas_ != null && slotId < object.itemDatas_.length)
            {
               object.itemDatas_[slotId] = -1;
            }
         }
      }

      public function useItem(time:int, objectId:int, slotId:int, posX:Number, posY:Number) : void
      {
         var owner:GameObject = this.gs_ != null && this.gs_.map != null ? this.gs_.map.goDict_[objectId] : null;
         this.enqueueInvOp(objectId,slotId,this.readSlotItem(owner,slotId),this.readSlotData(owner,slotId));
         if(this.player != null)
         {
            if(slotId == PotionInventoryModel.HEALTH_POTION_SLOT && this.player.healthPotionCount_ > 0)
            {
               this.player.healthPotionCount_--;
            }
            else if(slotId == PotionInventoryModel.MAGIC_POTION_SLOT && this.player.magicPotionCount_ > 0)
            {
               this.player.magicPotionCount_--;
            }
         }
         var useItemMess:UseItem = this.messages.require(USEITEM) as UseItem;
         useItemMess.time_ = time;
         useItemMess.slotObject_.objectId_ = objectId;
         useItemMess.slotObject_.slotId_ = slotId;
         useItemMess.itemUsePos_.x_ = posX;
         useItemMess.itemUsePos_.y_ = posY;
         this.serverConnection.sendMessage(useItemMess);
      }

      public function useItem_new(itemOwner:GameObject, slotId:int) : Boolean
      {
         var itemId:int = itemOwner.equipment_[slotId];
         var objectXML:XML = ObjectLibrary.xmlLibrary_[itemId];
         if(objectXML && (objectXML.hasOwnProperty("Consumable") || objectXML.hasOwnProperty("InvUse")))
         {
            this.applyUseItem(itemOwner,slotId,itemId,objectXML);
            SoundEffectLibrary.play("use_potion");
            return true;
         }
         SoundEffectLibrary.play("error");
         return false;
      }

      private function applyUseItem(owner:GameObject, slotId:int, objectType:int, itemData:XML) : void
      {
         this.enqueueInvOp(owner != null ? owner.objectId_ : -1,slotId,this.readSlotItem(owner,slotId),this.readSlotData(owner,slotId));
         var useItem:UseItem = this.messages.require(USEITEM) as UseItem;
         useItem.time_ = gs_.lastUpdate_;
         useItem.slotObject_.objectId_ = owner.objectId_;
         useItem.slotObject_.slotId_ = slotId;
         useItem.itemUsePos_.x_ = 0;
         useItem.itemUsePos_.y_ = 0;
         this.serverConnection.sendMessage(useItem);
         if(itemData.hasOwnProperty("Consumable"))
         {
            if(owner != null && owner.equipment_ != null && slotId >= 0 && slotId < owner.equipment_.length)
            {
               owner.equipment_[slotId] = -1;
               if(owner.itemDatas_ != null && slotId < owner.itemDatas_.length)
               {
                  owner.itemDatas_[slotId] = -1;
               }
            }
         }
      }
      
      public function move(player:Player, time:int) : void
      {
         var move:Move = this.messages.require(MOVE) as Move;
         move.time_ = time;
         move.newPosition_.x_ = player.x_;
         move.newPosition_.y_ = player.y_;
         this.serverConnection.sendMessage(move);
      }
      
      public function teleport(objectId:int) : void
      {
         var teleport:Teleport = this.messages.require(TELEPORT) as Teleport;
         teleport.objectId_ = objectId;
         this.serverConnection.sendMessage(teleport);
      }
      
      public function usePortal(objectId:int) : void
      {
         var usePortal:UsePortal = this.messages.require(USEPORTAL) as UsePortal;
         usePortal.objectId_ = objectId;
         this.serverConnection.sendMessage(usePortal);
      }
      
      public function buy(sellableObjectId:int, currencyType:int) : void
      {
         if(this.outstandingBuy_)
         {
            return;
         }
         var sObj:SellableObject = this.gs_.map.goDict_[sellableObjectId];
         if(sObj == null)
         {
            return;
         }
         trace("Indi: TODO I think this can be switched to a Boolean with no consequences");
         this.outstandingBuy_ = new OutstandingBuy(sObj.soldObjectInternalName(),sObj.price_,sObj.currency_);
         var buyMesssage:Buy = this.messages.require(BUY) as Buy;
         buyMesssage.objectId_ = sellableObjectId;
         this.serverConnection.sendMessage(buyMesssage);
      }

      public function gotoAck(time:int) : void
      {
         var gotoAck:GotoAck = this.messages.require(GOTOACK) as GotoAck;
         gotoAck.time_ = time;
         this.serverConnection.sendMessage(gotoAck);
      }

      public function chooseName(name:String) : void
      {
         var chooseName:ChooseName = this.messages.require(CHOOSENAME) as ChooseName;
         chooseName.name_ = name;
         this.serverConnection.sendMessage(chooseName);
      }
      
      public function editAccountList(accountListId:int, add:Boolean, objectId:int) : void
      {
         var eal:EditAccountList = this.messages.require(EDITACCOUNTLIST) as EditAccountList;
         eal.accountListId_ = accountListId;
         eal.add_ = add;
         eal.objectId_ = objectId;
         this.serverConnection.sendMessage(eal);
      }
      
      public function createGuild(name:String) : void
      {
         var createGuild:CreateGuild = this.messages.require(CREATEGUILD) as CreateGuild;
         createGuild.name_ = name;
         this.serverConnection.sendMessage(createGuild);
      }
      
      public function guildRemove(name:String) : void
      {
         var guildRemove:GuildRemove = this.messages.require(GUILDREMOVE) as GuildRemove;
         guildRemove.name_ = name;
         this.serverConnection.sendMessage(guildRemove);
      }
      
      public function guildInvite(name:String) : void
      {
         var guildInvite:GuildInvite = this.messages.require(GUILDINVITE) as GuildInvite;
         guildInvite.name_ = name;
         this.serverConnection.sendMessage(guildInvite);
      }
      
      public function escape() : void
      {
         if(this.playerId_ == -1 || this.gs_.map.name_ == "Nexus")
         {
            return;
         }
         this.serverConnection.sendMessage(this.messages.require(ESCAPE));
      }
      
      public function joinGuild(guildName:String) : void
      {
         var joinGuild:JoinGuild = this.messages.require(JOINGUILD) as JoinGuild;
         joinGuild.guildName_ = guildName;
         this.serverConnection.sendMessage(joinGuild);
      }
      
      public function changeGuildRank(name:String, rank:int) : void
      {
         var changeGuildRank:ChangeGuildRank = this.messages.require(CHANGEGUILDRANK) as ChangeGuildRank;
         changeGuildRank.name_ = name;
         changeGuildRank.guildRank_ = rank;
         this.serverConnection.sendMessage(changeGuildRank);
      }

      public function requestTrade(name:String) : void
      {
         var requestTrade:RequestTrade = this.messages.require(REQUESTTRADE) as RequestTrade;
         requestTrade.name_ = name;
         this.serverConnection.sendMessage(requestTrade);
      }

      public function changeTrade(offer:Vector.<Boolean>) : void
      {
         var changeTrade:ChangeTrade = this.messages.require(CHANGETRADE) as ChangeTrade;
         changeTrade.offer_ = offer;
         this.serverConnection.sendMessage(changeTrade);
      }

      public function acceptTrade(myOffer:Vector.<Boolean>, yourOffer:Vector.<Boolean>) : void
      {
         var acceptTrade:AcceptTrade = this.messages.require(ACCEPTTRADE) as AcceptTrade;
         acceptTrade.myOffer_ = myOffer;
         acceptTrade.yourOffer_ = yourOffer;
         this.serverConnection.sendMessage(acceptTrade);
      }

      public function cancelTrade() : void
      {
         this.serverConnection.sendMessage(this.messages.require(CANCELTRADE));
      }

      private function onConnected() : void
      {
         var account:Account = StaticInjectorContext.getInjector().getInstance(Account);
         this.addTextLine.dispatch(new AddTextLineVO(Parameters.CLIENT_CHAT_NAME,"Connected!"));
         var hello:Hello = this.messages.require(HELLO) as Hello;
         hello.buildVersion_ = Parameters.BUILD_VERSION;
         hello.gameId_ = this.gameId_;
         hello.username_ = account.getUsername();
         hello.password_ = account.getPassword();
         hello.mapJSON_ = this.mapJSON_ == null?"":this.mapJSON_;
         this.serverConnection.sendMessage(hello);
      }
      
      private function onCreateSuccess(createSuccess:CreateSuccess) : void
      {
         this.playerId_ = createSuccess.objectId_;
         this.charId_ = createSuccess.charId_;
         this.gs_.initialize();
         this.createCharacter_ = false;
      }
      
      private function onDamage(damage:Damage) : void
      {
         if (!Parameters.data_.allyDamage && damage.targetId_ != playerId_)
         {
            return;
         }
         var map:Map = this.gs_.map;
         var target:GameObject = map.goDict_[damage.targetId_];
         if(target != null)
         {
            target.damage(damage.damageAmount_,damage.effects_,null);
         }
      }
      
      private function onServerPlayerShoot(serverPlayerShoot:ServerPlayerShoot) : void
      {
         var needsAck:Boolean = serverPlayerShoot.ownerId_ == this.playerId_;
         if (!Parameters.data_.allyShots && !needsAck)
         {
            return;
         }
         if(needsAck)
         {
            this.shootAck(this.gs_.lastUpdate_);
         }
         var owner:GameObject = this.gs_.map.goDict_[serverPlayerShoot.ownerId_];
         if(owner == null)
         {
            trace("onServerPlayerShoot: unknown owner " + serverPlayerShoot.ownerId_);
            return;
         }
         for (var i:int = 0; i < serverPlayerShoot.damageList_.length; i++)
         {
            var proj:Projectile = FreeList.newObject(Projectile) as Projectile;
            var bulletId:int = serverPlayerShoot.ownerId_ == this.playerId_ ? serverPlayerShoot.bulletId_ + i : 0;
            proj.reset(serverPlayerShoot.containerType_, 0, serverPlayerShoot.ownerId_, bulletId, serverPlayerShoot.angle_ + (serverPlayerShoot.angleInc_ * i), this.gs_.lastUpdate_);
            proj.setDamage(serverPlayerShoot.damageList_[i]);
            this.gs_.map.addObj(proj, serverPlayerShoot.startingPos_.x_, serverPlayerShoot.startingPos_.y_);
         }
         owner.setAttack(serverPlayerShoot.containerType_, serverPlayerShoot.angle_);
      }
      
      private function onAllyShoot(allyShoot:AllyShoot) : void
      {
         if (!Parameters.data_.allyShots)
         {
            return;
         }
         var i:int;
         var owner:GameObject = this.gs_.map.goDict_[allyShoot.ownerId_];
         var weaponXML:XML =  ObjectLibrary.xmlLibrary_[allyShoot.containerType_];
         if(owner == null || weaponXML == null)
         {
            trace("onAllyShoot: missing owner or weaponXML ownerId=" + allyShoot.ownerId_ + " container=" + allyShoot.containerType_);
            return;
         }
         var arcGap:Number = (Boolean(weaponXML.hasOwnProperty("ArcGap"))?Number(weaponXML.ArcGap):11.25) * Trig.toRadians;
         var numShots:int = Boolean(weaponXML.hasOwnProperty("NumProjectiles"))?int(int(weaponXML.NumProjectiles)):int(1);
         var totalArc:Number = arcGap * (numShots - 1);
         var angle:Number = allyShoot.angle_ - totalArc / 2;
         var startId:int = Projectile.nextFakeBulletId_;
         Projectile.nextFakeBulletId_ += numShots;
         for (i = 0; i < numShots; i++)
         {
            var proj:Projectile = FreeList.newObject(Projectile) as Projectile;
            proj.reset(allyShoot.containerType_,0,allyShoot.ownerId_, startId + i, angle,this.gs_.lastUpdate_);
            this.gs_.map.addObj(proj,owner.x_,owner.y_);
            angle = angle + arcGap;
         }
         owner.setAttack(allyShoot.containerType_,allyShoot.angle_);
      }
      
      private function onEnemyShoot(enemyShoot:EnemyShoot) : void
      {
         var proj:Projectile = null;
         var angle:Number = NaN;
         var owner:GameObject = this.gs_.map.goDict_[enemyShoot.ownerId_];
         this.shootAck(this.gs_.lastUpdate_);
         if(owner == null)
         {
            trace("onEnemyShoot: unknown owner " + enemyShoot.ownerId_);
            return;
         }
         if(ObjectLibrary.propsLibrary_[owner.objectType_] == null)
         {
            trace("onEnemyShoot: no props for owner type " + owner.objectType_ + " id=" + enemyShoot.ownerId_);
            owner.setAttack(owner.objectType_,enemyShoot.angle_ + enemyShoot.angleInc_ * ((enemyShoot.numShots_ - 1) / 2));
            return;
         }
         for(var i:int = 0; i < enemyShoot.numShots_; i++)
         {
            proj = FreeList.newObject(Projectile) as Projectile;
            angle = enemyShoot.angle_ + enemyShoot.angleInc_ * i;
            proj.reset(owner.objectType_,enemyShoot.bulletType_,enemyShoot.ownerId_, enemyShoot.bulletId_ + i, angle,this.gs_.lastUpdate_);
            proj.setDamage(enemyShoot.damage_);
            this.gs_.map.addObj(proj,enemyShoot.startingPos_.x_,enemyShoot.startingPos_.y_);
         }
         owner.setAttack(owner.objectType_,enemyShoot.angle_ + enemyShoot.angleInc_ * ((enemyShoot.numShots_ - 1) / 2));
      }

      private function dropObject(obj:ObjectDropData) : void
      {
         var go:GameObject = this.gs_.map.goDict_[obj.objectId_];
         if (obj.explode_ && go is Character) {
            (go as Character).explode();
         }
         this.gs_.map.removeObj(obj.objectId_);
      }
      
      private function addObject(obj:ObjectData) : void
      {
         var map:Map = this.gs_.map;
         var go:GameObject = ObjectLibrary.getObjectFromType(obj.objectType_);
         if(go == null)
         {
            trace("unhandled object type: " + obj.objectType_ + ", using placeholder");
            go = new GameObject(null);
            go.objectType_ = obj.objectType_;
         }
         var status:ObjectStatusData = obj.status_;
         go.setObjectId(status.objectId_);
         map.addObj(go,status.pos_.x_,status.pos_.y_);
         if(go is Player)
         {
            this.handleNewPlayer(go as Player,map);
         }
         try
         {
            this.processObjectStatus(status);
         }
         catch(error:Error)
         {
            trace("addObject: processObjectStatus failed type=" + obj.objectType_ + " " + error.getStackTrace());
         }
         if(go.props_.static_ && go.props_.occupySquare_ && !go.props_.noMiniMap_)
         {
            this.updateGameObjectTileSignal.dispatch(new UpdateGameObjectTileVO(go.x_,go.y_,go));
         }
      }
      
      private function handleNewPlayer(player:Player, map:Map) : void
      {
         this.setPlayerSkinTemplate(player,0);
         if(player.objectId_ == this.playerId_)
         {
            this.player = player;
            this.model.player = player;
            map.player_ = player;
            this.gs_.setFocus(player);
            this.setGameFocus.dispatch(this.playerId_.toString());
         }
      }
      
      private function onUpdate(update:Update) : void
      {
         var i:int = 0;
         var tile:GroundTileData = null;
         for(i = 0; i < update.tiles_.length; i++)
         {
            tile = update.tiles_[i];
            this.gs_.map.setGroundTile(tile.x_,tile.y_,tile.type_);
            this.updateGroundTileSignal.dispatch(new UpdateGroundTileVO(tile.x_,tile.y_,tile.type_));
         }
         for(i = 0; i < update.newObjs_.length; i++)
         {
            this.addObject(update.newObjs_[i]);
         }
         for(i = 0; i < update.drops_.length; i++)
         {
            this.dropObject(update.drops_[i]);
         }
      }
      
      private function onNotification(notification:Notification) : void
      {
         // used to be queued
         var text:CharacterStatusText = null;
         var go:GameObject = this.gs_.map.goDict_[notification.objectId_];

         if(go != null)
         {
            if (!Parameters.data_.allyNotifs && go.props_.isPlayer_ && notification.objectId_ != playerId_)
            {
               return;
            }
            text = new CharacterStatusText(go,notification.text_,notification.color_,2000);
            this.gs_.map.mapOverlay_.addStatusText(text);
            if(go == this.player && notification.text_ == "Quest Complete!")
            {
               this.gs_.map.quest_.completed();
            }
         }
      }
      
      private function onGlobalNotification(notification:GlobalNotification) : void
      {
         switch(notification.text)
         {
            case "yellow":
               ShowKeySignal.instance.dispatch(Key.YELLOW);
               break;
            case "red":
               ShowKeySignal.instance.dispatch(Key.RED);
               break;
            case "green":
               ShowKeySignal.instance.dispatch(Key.GREEN);
               break;
            case "purple":
               ShowKeySignal.instance.dispatch(Key.PURPLE);
               break;
            case "showKeyUI":
               ShowKeyUISignal.instance.dispatch();
               break;
         }
      }
      
      private function onNewTick(newTick:NewTick) : void
      {
         var objectStatus:ObjectStatusData = null;
         if(this.jitterWatcher_ != null)
         {
            this.jitterWatcher_.record();
         }
         if(this.gs_ != null && this.gs_.map != null)
         {
            this.gs_.map.movesRequested_++;
         }
         for each(objectStatus in newTick.statuses_)
         {
            try
            {
               this.processObjectStatus(objectStatus);
            }
            catch(error:Error)
            {
               trace("onNewTick: processObjectStatus failed objectId=" + objectStatus.objectId_ + " " + error.getStackTrace());
            }
         }
         if (newTick.playerStats_.length > 0 && this.gs_.map.player_ != null)
         {
            try
            {
               this.updateGameObject(this.gs_.map.player_, newTick.playerStats_, true);
            }
            catch(error:Error)
            {
               trace("onNewTick: playerStats failed " + error.getStackTrace());
            }
         }
      }
      
      private function onShowEffect(showEffect:ShowEffect) : void
      {
         var go:GameObject = null;
         var e:ParticleEffect = null;
         var start:Point = null;
         var map:Map = this.gs_.map;
         switch(showEffect.effectType_)
         {
            case ShowEffect.HEAL_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go == null)
               {
                  break;
               }
               map.addObj(new HealEffect(go,showEffect.color_),go.x_,go.y_);
               break;
            case ShowEffect.TELEPORT_EFFECT_TYPE:
               map.addObj(new TeleportEffect(), showEffect.pos1_.x_, showEffect.pos1_.y_);
               break;
            case ShowEffect.STREAM_EFFECT_TYPE:
               e = new StreamEffect(showEffect.pos1_,showEffect.pos2_,showEffect.color_);
               map.addObj(e,showEffect.pos1_.x_,showEffect.pos1_.y_);
               break;
            case ShowEffect.THROW_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go != null)
               {
                  start = new Point(go.x_,go.y_);
               }
               else if(!showEffect.hasPos2_)
               {
                  start = showEffect.pos2_.toPoint();
               }
               else
               {
                  start = showEffect.pos1_.toPoint();
               }
               var duration:int = showEffect.hasPos2_?int(showEffect.pos2_.x_):1500;
               e = new ThrowEffect(start,showEffect.pos1_.toPoint(),showEffect.color_,duration);
               map.addObj(e,start.x,start.y);
               break;
            case ShowEffect.NOVA_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go == null)
               {
                  break;
               }
               e = new NovaEffect(go,showEffect.pos1_.x_,showEffect.color_);
               map.addObj(e,go.x_,go.y_);
               break;
            case ShowEffect.POISON_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if (go == null) {
                  break;
               }
               e = new PoisonEffect(go, showEffect.color_);
               map.addObj(e, go.x_, go.y_);
               break;
            case ShowEffect.LINE_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if (go == null) {
                  break;
               }
               e = new LineEffect(go, showEffect.pos1_, showEffect.color_);
               map.addObj(e, showEffect.pos1_.x_, showEffect.pos1_.y_);
               break;
            case ShowEffect.BURST_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go == null)
               {
                  break;
               }
               e = new BurstEffect(go,showEffect.pos1_,showEffect.pos2_,showEffect.color_);
               map.addObj(e,showEffect.pos1_.x_,showEffect.pos1_.y_);
               break;
            case ShowEffect.FLOW_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if (go == null) {
                  break;
               }
               e = new FlowEffect(showEffect.pos1_, go, showEffect.color_);
               map.addObj(e, showEffect.pos1_.x_, showEffect.pos1_.y_);
               break;
            case ShowEffect.RING_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go == null)
               {
                  break;
               }
               e = new RingEffect(go,showEffect.pos1_.x_,showEffect.color_);
               map.addObj(e,go.x_,go.y_);
               break;
            case ShowEffect.LIGHTNING_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go == null)
               {
                  break;
               }
               e = new LightningEffect(go,showEffect.pos1_,showEffect.color_,showEffect.pos2_.x_);
               map.addObj(e,go.x_,go.y_);
               break;
            case ShowEffect.COLLAPSE_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go == null)
               {
                  break;
               }
               e = new CollapseEffect(go,showEffect.pos1_,showEffect.pos2_,showEffect.color_);
               map.addObj(e,showEffect.pos1_.x_,showEffect.pos1_.y_);
               break;
            case ShowEffect.CONEBLAST_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go == null)
               {
                  break;
               }
               e = new ConeBlastEffect(go,showEffect.pos1_,showEffect.pos2_.x_,showEffect.color_);
               map.addObj(e,go.x_,go.y_);
               break;
            case ShowEffect.JITTER_EFFECT_TYPE:
               this.gs_.camera_.startJitter();
               break;
            case ShowEffect.FLASH_EFFECT_TYPE:
               go = map.goDict_[showEffect.targetObjectId_];
               if(go == null)
               {
                  break;
               }
               go.flash_ = new FlashDescription(getTimer(),showEffect.color_,showEffect.pos1_.x_,showEffect.pos1_.y_);
               break;
            case ShowEffect.THROW_PROJECTILE_EFFECT_TYPE:
               start = showEffect.pos1_.toPoint();
               e = new ThrowProjectileEffect(showEffect.color_,showEffect.pos2_.toPoint(),showEffect.pos1_.toPoint());
               map.addObj(e,start.x,start.y);
               break;
            default:
               trace("ERROR: Unknown Effect type: " + showEffect.effectType_);
         }
      }

      private function skipWrongTypeStat(go:GameObject, typed:Object, statType:int, expected:String) : Boolean
      {
         if(typed == null)
         {
            trace("updateGameObject: skipping stat " + statType + " (" + expected + " required) objectId=" + go.objectId_ + " type=" + go.objectType_);
            return true;
         }
         return false;
      }

      private function updateGameObject(go:GameObject, stats:Vector.<StatData>, isMyObject:Boolean) : void
      {
         var stat:StatData = null;
         var value:int = 0;
         var index:int = 0;
         if(go == null)
         {
            trace("updateGameObject: null object");
            return;
         }
         var player:Player = go as Player;
         var merchant:Merchant = go as Merchant;
         var sellable:SellableObject = go as SellableObject;
         var portal:Portal = go as Portal;
         for each(stat in stats)
         {
            value = stat.statValue_;
            switch(stat.statType_)
            {
               case StatData.MAX_HP_STAT:
                  go.maxHP_ = value;
                  continue;
               case StatData.HP_STAT:
                  go.hp_ = value;
                  continue;
               case StatData.SIZE_STAT:
                  go.setSize(value);
                  continue;
               case StatData.MAX_MP_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.maxMP_ = value;
                  continue;
               case StatData.MP_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.mp_ = value;
                  continue;
               case StatData.NEXT_LEVEL_EXP_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.nextLevelExp_ = value;
                  continue;
               case StatData.EXP_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.exp_ = value;
                  continue;
               case StatData.LEVEL_STAT:
                  go.level_ = value;
                  continue;
               case StatData.ATTACK_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.attack_ = value;
                  continue;
               case StatData.DEFENSE_STAT:
                  go.defense_ = value;
                  continue;
               case StatData.SPEED_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.speed_ = value;
                  continue;
               case StatData.DEXTERITY_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.dexterity_ = value;
                  continue;
               case StatData.VITALITY_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.vitality_ = value;
                  continue;
               case StatData.WISDOM_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.wisdom_ = value;
                  continue;
               case StatData.CONDITION_STAT:
                  go.condition_ = value;
                  continue;
               case StatData.INVENTORY_0_STAT:
               case StatData.INVENTORY_1_STAT:
               case StatData.INVENTORY_2_STAT:
               case StatData.INVENTORY_3_STAT:
               case StatData.INVENTORY_4_STAT:
               case StatData.INVENTORY_5_STAT:
               case StatData.INVENTORY_6_STAT:
               case StatData.INVENTORY_7_STAT:
               case StatData.INVENTORY_8_STAT:
               case StatData.INVENTORY_9_STAT:
               case StatData.INVENTORY_10_STAT:
               case StatData.INVENTORY_11_STAT:
                  if(go.equipment_ == null)
                  {
                     trace("updateGameObject: skipping inventory stat " + stat.statType_ + " on object without equipment objectId=" + go.objectId_);
                     continue;
                  }
                  go.equipment_[stat.statType_ - StatData.INVENTORY_0_STAT] = value;
                  continue;
               case StatData.NUM_STARS_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.numStars_ = value;
                  continue;
               case StatData.NAME_STAT:
                  if(go.name_ != stat.strStatValue_)
                  {
                     go.name_ = stat.strStatValue_;
                     go.nameBitmapData_ = null;
                  }
                  continue;
               case StatData.TEX1_STAT:
                  go.setTex1(value);
                  continue;
               case StatData.TEX2_STAT:
                  go.setTex2(value);
                  continue;
               case StatData.MERCHANDISE_TYPE_STAT:
                  if(this.skipWrongTypeStat(go,merchant,stat.statType_,"Merchant"))
                  {
                     continue;
                  }
                  merchant.setMerchandiseType(value);
                  continue;
               case StatData.CREDITS_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.setCredits(value);
                  continue;
               case StatData.MERCHANDISE_PRICE_STAT:
                  if(this.skipWrongTypeStat(go,sellable,stat.statType_,"SellableObject"))
                  {
                     continue;
                  }
                  sellable.setPrice(value);
                  continue;
               case StatData.ACTIVE_STAT:
                  if(this.skipWrongTypeStat(go,portal,stat.statType_,"Portal"))
                  {
                     continue;
                  }
                  portal.active_ = value != 0;
                  continue;
               case StatData.ACCOUNT_ID_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.accountId_ = value;
                  continue;
               case StatData.FAME_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.fame_ = value;
                  continue;
               case StatData.MERCHANDISE_CURRENCY_STAT:
                  if(this.skipWrongTypeStat(go,sellable,stat.statType_,"SellableObject"))
                  {
                     continue;
                  }
                  sellable.setCurrency(value);
                  continue;
               case StatData.CONNECT_STAT:
                  go.connectType_ = value;
                  continue;
               case StatData.MERCHANDISE_COUNT_STAT:
                  if(this.skipWrongTypeStat(go,merchant,stat.statType_,"Merchant"))
                  {
                     continue;
                  }
                  merchant.count_ = value;
                  merchant.untilNextMessage_ = 0;
                  continue;
               case StatData.MERCHANDISE_MINS_LEFT_STAT:
                  if(this.skipWrongTypeStat(go,merchant,stat.statType_,"Merchant"))
                  {
                     continue;
                  }
                  merchant.minsLeft_ = value;
                  merchant.untilNextMessage_ = 0;
                  continue;
               case StatData.MERCHANDISE_DISCOUNT_STAT:
                  if(this.skipWrongTypeStat(go,merchant,stat.statType_,"Merchant"))
                  {
                     continue;
                  }
                  merchant.discount_ = value;
                  merchant.untilNextMessage_ = 0;
                  continue;
               case StatData.MERCHANDISE_RANK_REQ_STAT:
                  if(this.skipWrongTypeStat(go,sellable,stat.statType_,"SellableObject"))
                  {
                     continue;
                  }
                  sellable.setRankReq(value);
                  continue;
               case StatData.MAX_HP_BOOST_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.maxHPBoost_ = value;
                  continue;
               case StatData.MAX_MP_BOOST_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.maxMPBoost_ = value;
                  continue;
               case StatData.ATTACK_BOOST_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.attackBoost_ = value;
                  continue;
               case StatData.DEFENSE_BOOST_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.defenseBoost_ = value;
                  continue;
               case StatData.SPEED_BOOST_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.speedBoost_ = value;
                  continue;
               case StatData.VITALITY_BOOST_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.vitalityBoost_ = value;
                  continue;
               case StatData.WISDOM_BOOST_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.wisdomBoost_ = value;
                  continue;
               case StatData.DEXTERITY_BOOST_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.dexterityBoost_ = value;
                  continue;
               case StatData.CHAR_FAME_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.charFame_ = value;
                  continue;
               case StatData.NEXT_CLASS_QUEST_FAME_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.nextClassQuestFame_ = value;
                  continue;
               case StatData.LEGENDARY_RANK_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.legendaryRank_ = value;
                  continue;
               case StatData.SINK_LEVEL_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  if(!isMyObject)
                  {
                     player.sinkLevel_ = value;
                  }
                  continue;
               case StatData.ALT_TEXTURE_STAT:
                  go.setAltTexture(value);
                  continue;
               case StatData.GUILD_NAME_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.setGuildName(stat.strStatValue_);
                  continue;
               case StatData.GUILD_RANK_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.guildRank_ = value;
                  continue;
               case StatData.BREATH_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.breath_ = value;
                  continue;

               case StatData.HEALTH_POTION_STACK_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.healthPotionCount_ = value;
                  continue;
               case StatData.MAGIC_POTION_STACK_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.magicPotionCount_ = value;
                  continue;
               case StatData.TEXTURE_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.skinId != value && this.setPlayerSkinTemplate(player,value);
                  continue;
               case StatData.HASBACKPACK_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.hasBackpack_ = Boolean(value);
                  if(isMyObject)
                  {
                     this.updateBackpackTab.dispatch(Boolean(value));
                  }
                  continue;
               case StatData.BACKPACK_0_STAT:
               case StatData.BACKPACK_1_STAT:
               case StatData.BACKPACK_2_STAT:
               case StatData.BACKPACK_3_STAT:
               case StatData.BACKPACK_4_STAT:
               case StatData.BACKPACK_5_STAT:
               case StatData.BACKPACK_6_STAT:
               case StatData.BACKPACK_7_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  index = stat.statType_ - StatData.BACKPACK_0_STAT + GeneralConstants.NUM_EQUIPMENT_SLOTS + GeneralConstants.NUM_INVENTORY_SLOTS;
                  if(player.equipment_ == null || index < 0 || index >= player.equipment_.length)
                  {
                     trace("updateGameObject: skipping backpack stat " + stat.statType_ + " objectId=" + go.objectId_);
                     continue;
                  }
                  player.equipment_[index] = value;
                  continue;
               case StatData.ITEMDATA_0_STAT:
               case StatData.ITEMDATA_1_STAT:
               case StatData.ITEMDATA_2_STAT:
               case StatData.ITEMDATA_3_STAT:
               case StatData.ITEMDATA_4_STAT:
               case StatData.ITEMDATA_5_STAT:
               case StatData.ITEMDATA_6_STAT:
               case StatData.ITEMDATA_7_STAT:
               case StatData.ITEMDATA_8_STAT:
               case StatData.ITEMDATA_9_STAT:
               case StatData.ITEMDATA_10_STAT:
               case StatData.ITEMDATA_11_STAT:
               case StatData.ITEMDATA_12_STAT:
               case StatData.ITEMDATA_13_STAT:
               case StatData.ITEMDATA_14_STAT:
               case StatData.ITEMDATA_15_STAT:
               case StatData.ITEMDATA_16_STAT:
               case StatData.ITEMDATA_17_STAT:
               case StatData.ITEMDATA_18_STAT:
               case StatData.ITEMDATA_19_STAT:
                  if(go.itemDatas_ == null)
                  {
                     trace("updateGameObject: skipping itemdata stat " + stat.statType_ + " on object without itemDatas objectId=" + go.objectId_);
                     continue;
                  }
                  go.itemDatas_[stat.statType_ - StatData.ITEMDATA_0_STAT] = value;
                  continue;
               case StatData.NAME_CHOSEN_STAT:
                  if(this.skipWrongTypeStat(go,player,stat.statType_,"Player"))
                  {
                     continue;
                  }
                  player.nameChosen_ = value != 0;
                  go.nameBitmapData_ = null;
                  continue;
               default:
                  trace("unhandled stat: " + stat.statType_);
                  continue;
            }
         }
      }
      
      private function setPlayerSkinTemplate(player:Player, skinId:int) : void
      {
         var message:Reskin = this.messages.require(RESKIN) as Reskin;
         message.skinID = skinId;
         message.player = player;
         message.consume();
      }
      
      private function processObjectStatus(objectStatus:ObjectStatusData) : void
      {
         var pLevel:int = -1;
         var pExp:int = -1;
         var pFame:int = -1;
         var type:CharacterClass = null;
         var map:Map = this.gs_.map;
         var go:GameObject = map.goDict_[objectStatus.objectId_];
         if(go == null)
         {
            trace("missing object: " + objectStatus.objectId_);
            return;
         }
         var isMyObject:Boolean = objectStatus.objectId_ == this.playerId_;
         if(!isMyObject)
         {
            go.onTickPos(objectStatus.pos_.x_,objectStatus.pos_.y_);
         }
         var player:Player = go as Player;
         if(player != null)
         {
            pLevel = player.level_;
            pExp = player.exp_;
            pFame = player.charFame_;
         }
         this.updateGameObject(go,objectStatus.stats_,isMyObject);
         if(player != null && pLevel != -1)
         {
            if(player.level_ > pLevel)
            {
               if(isMyObject)
               {
                  player.handleLevelUp();
               }
               else
               {
                  if (Parameters.data_.allyNotifs)
                  {
                     player.levelUpEffect("Level Up!");
                  }
               }
            }
            else if(player.exp_ > pExp)
            {
               if (!Parameters.data_.allyNotifs && !isMyObject)
               {
                  return;
               }
               player.handleExpUp(player.exp_ - pExp);
               if (player.charFame_ > pFame) {
                  player.handleFameUp(player.charFame_ - pFame)
               }
            }
         }
      }
      
      private function onText(text:Text) : void
      {
         var go:GameObject = null;
         var colors:Vector.<uint> = null;
         var speechBalloonvo:AddSpeechBalloonVO = null;
         var textString:String = text.text_;
         if(text.objectId_ >= 0)
         {
            go = this.gs_.map.goDict_[text.objectId_];
            if(go != null)
            {
               colors = NORMAL_SPEECH_COLORS;
               if(go.props_.isEnemy_)
               {
                  colors = ENEMY_SPEECH_COLORS;
               }
               else if(text.recipient_ == Parameters.GUILD_CHAT_NAME)
               {
                  colors = GUILD_SPEECH_COLORS;
               }
               else if(text.recipient_ != "")
               {
                  colors = TELL_SPEECH_COLORS;
               }
               speechBalloonvo = new AddSpeechBalloonVO(go,textString,colors[0],1,colors[1],1,colors[2],text.bubbleTime_,false,true);
               this.addSpeechBalloon.dispatch(speechBalloonvo);
            }
         }
         this.addTextLine.dispatch(new AddTextLineVO(text.name_,textString,text.objectId_,text.numStars_,text.recipient_));
      }
      
      private function onInvResult(invResult:InvResult) : void
      {
         var op:Object = this.pendingInvOps_.length > 0 ? this.pendingInvOps_.shift() : null;
         if(invResult.result_ != 0)
         {
            if(op != null)
            {
               this.restoreInvOp(op);
            }
            this.handleInvFailure();
         }
      }
      
      private function enqueueInvOp(objectId1:int, slot1:int, item1:int, data1:int, objectId2:int = -1, slot2:int = -1, item2:int = -1, data2:int = -1) : void
      {
         this.pendingInvOps_.push({
            "objectId1":objectId1,
            "slot1":slot1,
            "item1":item1,
            "data1":data1,
            "objectId2":objectId2,
            "slot2":slot2,
            "item2":item2,
            "data2":data2,
            "healthPots":this.player != null ? this.player.healthPotionCount_ : 0,
            "magicPots":this.player != null ? this.player.magicPotionCount_ : 0
         });
      }
      
      private function restoreInvOp(op:Object) : void
      {
         this.restoreSlot(op.objectId1,op.slot1,op.item1,op.data1);
         if(op.objectId2 >= 0 && op.slot2 >= 0)
         {
            this.restoreSlot(op.objectId2,op.slot2,op.item2,op.data2);
         }
         if(this.player != null && (op.slot1 == PotionInventoryModel.HEALTH_POTION_SLOT || op.slot1 == PotionInventoryModel.MAGIC_POTION_SLOT))
         {
            this.player.healthPotionCount_ = op.healthPots;
            this.player.magicPotionCount_ = op.magicPots;
         }
      }
      
      private function restoreSlot(objectId:int, slotId:int, item:int, data:int) : void
      {
         if(objectId < 0 || slotId < 0 || this.gs_ == null || this.gs_.map == null)
         {
            return;
         }
         var go:GameObject = this.gs_.map.goDict_[objectId];
         if(go == null || go.equipment_ == null || slotId >= go.equipment_.length)
         {
            return;
         }
         go.equipment_[slotId] = item;
         if(go.itemDatas_ != null && slotId < go.itemDatas_.length)
         {
            go.itemDatas_[slotId] = data;
         }
      }
      
      private function readSlotItem(go:GameObject, slotId:int) : int
      {
         if(go == null || go.equipment_ == null || slotId < 0 || slotId >= go.equipment_.length)
         {
            return -1;
         }
         return go.equipment_[slotId];
      }
      
      private function readSlotData(go:GameObject, slotId:int) : int
      {
         if(go == null || go.itemDatas_ == null || slotId < 0 || slotId >= go.itemDatas_.length)
         {
            return -1;
         }
         return go.itemDatas_[slotId];
      }
      
      private function handleInvFailure() : void
      {
         SoundEffectLibrary.play("error");
         if(this.gs_ != null && this.gs_.hudView != null)
         {
            this.gs_.hudView.draw();
         }
         if(this.updateHUD != null && this.player != null)
         {
            this.updateHUD.dispatch(this.player);
         }
      }
      
      private function onReconnect(reconnect:Reconnect) : void
      {
         var gameID:int = reconnect.gameId_;
         var createChar:Boolean = this.createCharacter_;
         var charId:int = this.charId_;
         var reconnectEvent:ReconnectEvent = new ReconnectEvent(gameID,createChar,charId);
         this.gs_.dispatchEvent(reconnectEvent);
      }
      
      private function parseXML(xmlString:String) : void
      {
         var extraXML:XML = XML(xmlString);
         GroundLibrary.parseFromXML(extraXML);
         ObjectLibrary.parseFromXML(extraXML);
         ObjectLibrary.parseFromXML(extraXML);
      }
      
      private function onMapInfo(mapInfo:MapInfo) : void
      {
         this.gs_.applyMapInfo(mapInfo);
         this.rand_ = new Random(mapInfo.fp_);
         if(this.createCharacter_)
         {
            this.create();
         }
         else
         {
            this.load();
         }
      }
      
      private function onDeath(death:Death) : void
      {
         // keep for now, seems to fix the death issue
         //disconnect();

         this.death = death;
         var data:BitmapData = new BitmapData(this.gs_.stage.stageWidth,this.gs_.stage.stageHeight);
         data.draw(this.gs_);
         death.background = data;
         if(!this.gs_.isEditor)
         {
            this.handleDeath.dispatch(death);
         }
      }
      
      private function onBuyResult(buyResult:BuyResult) : void
      {
         if(buyResult.result_ == BuyResult.SUCCESS_BRID)
         {
            if(this.outstandingBuy_)
            {
            }
         }
         this.outstandingBuy_ = null;
         switch(buyResult.result_)
         {
            case BuyResult.DIALOG_BRID:
               StaticInjectorContext.getInjector().getInstance(OpenDialogSignal).dispatch(new MessageCloseDialog("Purchase Error", buyResult.resultString_));
               break;
            default:
               this.addTextLine.dispatch(new AddTextLineVO(buyResult.result_ == BuyResult.SUCCESS_BRID?Parameters.SERVER_CHAT_NAME:Parameters.ERROR_CHAT_NAME,buyResult.resultString_));
         }
      }
      
      private function onAccountList(accountList:AccountList) : void
      {
         if(accountList.accountListId_ == 0)
         {
            this.gs_.map.party_.setStars(accountList);
         }
         if(accountList.accountListId_ == 1)
         {
            this.gs_.map.party_.setIgnores(accountList);
         }
      }
      
      private function onQuestObjId(questObjId:QuestObjId) : void
      {
         this.gs_.map.quest_.setObject(questObjId.objectId_);
      }
      
      private function onAoe(aoe:Aoe) : void
      {
         var d:int = 0;
         var effects:Vector.<uint> = null;
         var e:AOEEffect = new AOEEffect(aoe.pos_.toPoint(),aoe.radius_,aoe.color_);
         this.gs_.map.addObj(e,aoe.pos_.x_,aoe.pos_.y_);
         if(this.player.isInvincible())
         {
            this.aoeAck(this.gs_.lastUpdate_,this.player.x_,this.player.y_);
            return;
         }
         var hit:Boolean = this.player.distTo(aoe.pos_) < aoe.radius_;
         if(hit)
         {
            d = GameObject.damageWithDefense(aoe.damage_,this.player.defense_,false,this.player.condition_);
            effects = null;
            if(aoe.effect_ != 0)
            {
               effects = new Vector.<uint>();
               effects.push(aoe.effect_);
            }
            this.player.damage(d,effects,null);
         }
         this.aoeAck(this.gs_.lastUpdate_,this.player.x_,this.player.y_);
      }

      private function onGuildResult(guildResult:GuildResult) : void
      {
         this.addTextLine.dispatch(new AddTextLineVO(Parameters.ERROR_CHAT_NAME,guildResult.errorText_));
         this.gs_.dispatchEvent(new GuildResultEvent(guildResult.success_,guildResult.errorText_));
      }

      private function onNameResult(nameResult:NameResult) : void
      {
         this.gs_.dispatchEvent(new NameResultEvent(nameResult));
      }
      
      private function onInvitedToGuild(invitedToGuild:InvitedToGuild) : void
      {
         if(Parameters.data_.showGuildInvitePopup)
         {
            this.gs_.hudView.interactPanel.setOverride(new GuildInvitePanel(this.gs_,invitedToGuild.name_,invitedToGuild.guildName_));
         }
         this.addTextLine.dispatch(new AddTextLineVO("","You have been invited by " + invitedToGuild.name_ + " to join the guild " + invitedToGuild.guildName_ + ".\n  If you wish to join type \"/join " + invitedToGuild.guildName_ + "\""));
      }
      
      private function onTradeRequested(tradeRequested:TradeRequested) : void
      {
         if(Parameters.data_.showTradePopup)
         {
            this.gs_.hudView.interactPanel.setOverride(new TradeRequestPanel(this.gs_,tradeRequested.name_));
         }
         this.addTextLine.dispatch(new AddTextLineVO("",tradeRequested.name_ + " wants to " + "trade with you.  Type \"/trade " + tradeRequested.name_ + "\" to trade."));
      }

      private function onTradeStart(tradeStart:TradeStart) : void
      {
         this.gs_.hudView.startTrade(this.gs_,tradeStart);
      }

      private function onTradeChanged(tradeChanged:TradeChanged) : void
      {
         this.gs_.hudView.tradeChanged(tradeChanged);
      }

      private function onTradeDone(tradeDone:TradeDone) : void
      {
         this.gs_.hudView.tradeDone();
         this.addTextLine.dispatch(new AddTextLineVO("",tradeDone.description_));
      }

      private function onTradeAccepted(tradeAccepted:TradeAccepted) : void
      {
         this.gs_.hudView.tradeAccepted(tradeAccepted);
      }

      private function onPlaySound(playSound:PlaySound) : void
      {
         SoundEffectLibrary.play(playSound.sound_);
      }
      
      private function onClosed() : void
      {
         this.gs_.closed.dispatch();
      }

      private function onError(error:String) : void
      {
         this.addTextLine.dispatch(new AddTextLineVO(Parameters.ERROR_CHAT_NAME,error));
      }
      
      private function onFailure(event:Failure) : void
      {
         switch(event.errorId_)
         {
            case Failure.INCORRECT_VERSION:
               this.handleIncorrectVersionFailure(event);
               break;
            case Failure.FORCE_CLOSE_GAME:
               this.handleForceCloseGameFailure(event);
               break;
            case Failure.INVALID_TELEPORT_TARGET:
               this.handleInvalidTeleportTarget(event);
               break;
            default:
               this.handleDefaultFailure(event);
         }
      }
      
      private function handleInvalidTeleportTarget(event:Failure) : void
      {
         this.addTextLine.dispatch(new AddTextLineVO(Parameters.ERROR_CHAT_NAME,event.errorDescription_));
         this.player.nextTeleportAt_ = 0;
      }
      
      private function handleForceCloseGameFailure(event:Failure) : void
      {
         this.addTextLine.dispatch(new AddTextLineVO(Parameters.ERROR_CHAT_NAME,event.errorDescription_));
         if(this.playerId_ == -1)
         {
            this.leaveHandshake(event.errorDescription_);
         }
         else
         {
            this.gs_.closed.dispatch();
         }
      }
      
      private function handleIncorrectVersionFailure(event:Failure) : void
      {
         var dialog:Dialog = new Dialog("Client version: " + Parameters.BUILD_VERSION + "\nServer version: " + event.errorDescription_,"Client Update Needed","Ok",null);
         dialog.addEventListener(Dialog.BUTTON1_EVENT,this.onDoClientUpdate);
         this.gs_.stage.addChild(dialog);
      }
      
      private function handleDefaultFailure(event:Failure) : void
      {
         this.addTextLine.dispatch(new AddTextLineVO(Parameters.ERROR_CHAT_NAME,event.errorDescription_));
         if(this.playerId_ == -1)
         {
            this.leaveHandshake(event.errorDescription_);
         }
      }

      private function leaveHandshake(errorText:String) : void
      {
         this.gs_.closed.dispatch();
         StaticInjectorContext.getInjector().getInstance(OpenDialogSignal).dispatch(new ErrorDialog(errorText));
      }
      
      private function onDoClientUpdate(event:Event) : void
      {
         var dialog:Dialog = event.currentTarget as Dialog;
         dialog.parent.removeChild(dialog);
         this.gs_.closed.dispatch();
      }
   }
}
