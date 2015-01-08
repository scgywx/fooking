package 
{
	import adobe.utils.CustomActions;
	import flash.display.Sprite;
	import flash.events.Event;
	import flash.text.TextField;
	import flash.events.MouseEvent;
	import flash.events.ProgressEvent;
	import flash.events.IOErrorEvent;
	import flash.events.SecurityErrorEvent;
	import flash.external.ExternalInterface; 
	import flash.net.Socket;
	import flash.utils.ByteArray;
	
	/**
	 * ...
	 * @author wangxin
	 */
	public class Main extends Sprite 
	{
		private var m_Socket:Socket;
		private var m_ServerHost:String;
		private var m_ServerPort:Number;
		private var m_JSHandler:String;
		private var m_Buffer:ByteArray;
		private var m_Processer:Array;
		
		//其它定义
		public const NET_CHARSET:String = "utf-8";
		
		public function Main():void
		{
			//初始化参数
			var params:Object = root.loaderInfo.parameters;
			m_ServerHost	= params["server"];
			m_ServerPort	= params["port"];
			m_JSHandler		= params["handler"];
			
			//初始化socket
			trace("正在连接服务器.....");
			m_Buffer = new ByteArray();
			m_Socket = new Socket();
			m_Socket.addEventListener(Event.CONNECT, onNetConnect);
			m_Socket.addEventListener(ProgressEvent.SOCKET_DATA, onNetMessage);
			m_Socket.addEventListener(Event.CLOSE, onNetClose);
			m_Socket.addEventListener(SecurityErrorEvent.SECURITY_ERROR, onNetSecurityError);
			m_Socket.addEventListener(IOErrorEvent.IO_ERROR, onNetError);
			m_Socket.connect(m_ServerHost, m_ServerPort);
			
			//初始化js
			ExternalInterface.addCallback("sendMsg", sendMsgHandler);
			
			//通知JS初始化完成
			jsNotify("init", "Initialize complete");
		}
		
		//JS通知
		private function jsNotify(type:String, params:Object):void
		{
			if(ExternalInterface.available){
				ExternalInterface.call(m_JSHandler, type, params);
			}else {
				trace("cannot call js function");
			}
		}
		
		//socket连接事件
		private function onNetConnect(e:Event): void
		{
			trace("连接服务器成功!");
			jsNotify("connect", "Connection Successed");
		}
		
		//socket消息事件
		private function onNetMessage(e:ProgressEvent):void
		{
			m_Socket.readBytes(m_Buffer);
			while (m_Buffer.bytesAvailable >= 4) {
				var s:int = m_Buffer.position;
				var size:int = (m_Buffer[s + 0] << 24) | (m_Buffer[s + 1] << 16) | (m_Buffer[s + 2] << 8) | m_Buffer[s + 3];
				if (m_Buffer.bytesAvailable < size) {
					break;
				}
				var notuse:int = m_Buffer.readInt();
				var body:String = m_Buffer.readMultiByte(size, "utf-8");
				jsNotify("message", body);
			}
			if (!m_Buffer.bytesAvailable) {
				m_Buffer.clear();
			}
		}
		
		//socket关闭事件
		private function onNetClose(e:Event):void
		{
			jsNotify("close", "Connection close");
		}
		
		private function onNetSecurityError(e:SecurityErrorEvent):void
		{
			jsNotify("error", "securityError:" + e.toString());
		}
		
		private function onNetError(e:IOErrorEvent):void
		{
			jsNotify("error", "ioError:" + e.toString());
		}
		
		private function sendMsgHandler(str:String):void
		{
			jsNotify("sendmsg", str);
				
			var buf:ByteArray = new ByteArray();
			buf.writeMultiByte(str, "utf-8");
			m_Socket.writeByte((buf.length >> 24) & 0xff);
			m_Socket.writeByte((buf.length >> 16) & 0xff);
			m_Socket.writeByte((buf.length >> 8) & 0xff);
			m_Socket.writeByte(buf.length & 0xff);
			m_Socket.writeBytes(buf);
			m_Socket.flush();
		}
	}
	
}