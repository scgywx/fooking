function Game(server)
{
	var SEAT_MAX	= 5;
	var self 		= this;
	var connected	= false;
	
	this.socket		= null;
	this.server		= server;
	this.name		= "";
	this.tableid	= 0;
	this.seatid		= 0;
	this.state		= 0;
	this.current	= 0;
	this.point		= 0;
	this.total		= 0;
	this.seats		= {};
	this.users		= {};
	this.cards		= {};
	
	function _getPos(seat)
	{
		var pos = seat + 3 - self.seatid;
		if(pos == 0 || pos == 5){
			return 5;
		}else if(pos < 0){
			return 5 + pos;
		}else{
			return pos % 5;
		}
	}
	
	function _init()
	{
		self.socket = new WebSocket(self.server);
		self.socket.onopen = function(event) {
			console.log("connected");
			connected = true;
			_send("login", {
				name: self.name
			});
		};
		
		self.socket.onmessage = function(event) { 
			console.log('msg:',event);
			var data = event.data;
			var msg = data ? JSON.parse(data) : {};
			switch(msg.type){
				case "login": _login(msg.error, msg.data);break;
				case "list": _list(msg.error, msg.data);break;
				case "create": _create(msg.error, msg.data);break;
				case "join": _join(msg.error, msg.data);break;
				case "joinEx": _joinEx(msg.error, msg.data);break;
				case "leave": _leave(msg.error, msg.data);break;
				case "start": _start(msg.error, msg.data);break;
				case "look": _look(msg.error, msg.data);break;
				case "lookEx": _lookEx(msg.error, msg.data);break;
				case "follow": _follow(msg.error, msg.data);break;
				case "rise": _rise(msg.error, msg.data);break;
				case "discard": _discard(msg.error, msg.data);break;
				case "compare": _compare(msg.error, msg.data);break;
				case "over": _over(msg.error, msg.data);break;
				default: break;
			}
		};
		
		self.socket.onclose = function(event) {
			console.log('close:',event);
			if(connected){
				alert("断开与服务器连接");
			}else{
				alert("连接服务器失败!");
			}
			
			self.socket = null;
		};
		
		self.socket.onerror = function(event) { 
			console.log('error:',event); 
		};
	}
	
	function _send(type, data)
	{
		console.log("send data: ", data);
		if(self.socket){
			self.socket.send(JSON.stringify({
				type: type,
				data: data,
			}));
		}
	}
	
	function _login(err, data)
	{
		if(err){
			alert("登陆失败!");
			return ;
		}
		
		$('.welcome').hide();
		$('.game').show();
		
		self.list();
	}
	
	function _list(err, data)
	{
		var html = '';
		for(var x in data){
			var v = data[x];
			html+= '<div class="row">';
			html+= '<div class="col">' + v.title + '</div>';
			html+= '<div class="col">' + v.master + '</div>';
			html+= '<div class="col">' + v.mingold + '</div>';
			html+= '<div class="col">' + v.count + '</div>';
			html+= '<div class="col"><input onclick="game.join('+v.tableid+')" data-id="'+v.tableid+'" type="button" class="join" value="加入房间"/></div>';
			html+= '</div>';
			html+= '<div class="clear"></div>';
		}
		$('.game .box').html(html);
	}
	
	function _create(err, data)
	{
		if(err){
			alert("创建房间失败:"+ data);
			return ;
		}
		
		self.tableid = data.tableid;
		self.seatid = data.seatid;
		self.seats = data.seats;
		
		$('.game').hide();
		$('.table').show();
	}
	
	function _join(err, data)
	{
		if(err){
			alert("加入房间失败: " + data);
			return ;
		}
		
		self.tableid = data.tableid;
		self.seatid = data.seatid;
		self.seats = data.seats;
		
		for(var x in data.seats){
			self.users[data.seats[x].sid] = data.seats[x];
		}
		
		$('.game').hide();
		$('.table').show();
	}
	
	function _joinEx(err, data)
	{
		self.seats[data.seatid] = data.user;
		self.users[data.user.sid] = data.user;
	}
	
	function _leave(err, data)
	{
		if(err){
			alert("退出房间失败："+ data);
			return ;
		}
		
		self.tableid = 0;
		self.seatid = 0;
		self.seats = {};
	}
	
	function _start(err, data)
	{
		if(err){
			return ;
		}
		self.state = 1;
		self.current = data.current;
		
		for(var i = 1; i < SEAT_MAX; ++i)
		{
			if(self.seats[i]){
				var pos = _getPos(i);
				var html = '<div class="poker first"><img src="images/card_back_1.png"/></div>';
				html+= '<div class="poker"><img src="images/card_back_1.png"/></div>';
				html+= '<div class="poker"><img src="images/card_back_1.png"/></div>';				
				$('.c' + pos).html(html);
			} 
		}
	}
	
	function _follow(err, data)
	{
		if(err){
			alert("跟注失败："+ data);
			return ;
		}
		
		self.current = data.current;
		self.total = data.total;
		
	}
	
	function _rise(err, data)
	{
		if(err){
			alert("加注失败：" + data);
			return ;
		}
		
		self.current = data.current;
		self.total = data.total;
	}
	
	function _discard(err, data)
	{
		if(err){
			return ;
		}
		
		self.current = data.current;
	}
	
	function _look(err, data)
	{
		self.cards = data.cards;
		
		var html = '';
		for(var x in data.cards){
			var colors = [0, "a", "b", "c", "d"];
			var c = data.cards[x];
			var num = c % 13;
			var color = Math.ceil(c / 13);
			var f = ((x == 0) ? ' first' : '');
			
			switch(num){
				case 0:  num = 'K';break;
				case 1:  num = 'A';break;
				case 11: num = 'J';break;
				case 12: num = 'Q';break;
			}
			
			html+= '<div class="poker'+f+'"><img src="images/'+num+colors[color]+'.png"/></div>';
		}
		$('.c3').html(html);
	}
	
	function _lookEx(err, data)
	{
		
	}
	
	function _compare(err, data)
	{
		
	}
	
	function _over(err, data)
	{
		
	}
	
	this.login = function(name)
	{
		if(self.socket){
			return ;
		}
		
		self.name = name;
		
		_init();
	}
	
	this.list = function()
	{
		_send("list");
	}
	
	this.create = function(title, mingold)
	{
		_send("create", {
			title: title,
			mingold: mingold
		});
	}
	
	this.join = function(tableid)
	{
		_send("join", {
			tableid: tableid
		});
	}
	
	this.start = function()
	{
		_send("start");
	}
	
	this.follow = function()
	{
		_send("follow");
	}
	
	this.rise = function()
	{
		_send("rise");
	}
	
	this.discard = function()
	{
		_send("discard");
	}
	
	this.look = function()
	{
		_send("look");
	}
	
	this.leave = function()
	{
		_send("leave");
	}
	
	this.compare = function(seatid)
	{
		_send("compare", {
			seatid: seatid
		});
	}
}