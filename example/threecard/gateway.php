<?php
define('REDIS_HOST', '127.0.0.1');
define('REDIS_PORT', 6379);
define('ROUTER_HOST', '127.0.0.1');
define('ROUTER_PORT', 9010);
define('FOOKING', isset($_SERVER['SESSIONID']) ? true : false);
define('SID', isset($_SERVER['SESSIONID']) ? $_SERVER['SESSIONID'] : $_GET['sid']);
define('TABLE_MAX', 100);
define('SEAT_MAX', 5);
define('CARD_MAX', 3);
define('SEAT_STATE_DISCARD', 1);
define('SEAT_STATE_LOSE', 2);
define('SEAT_STATE_CLOSE', 3);

date_default_timezone_set('Asia/Shanghai');

require('api.php');

$event = empty($_SERVER['EVENT']) ? 0 : (int)$_SERVER['EVENT'];
if($event == 1){//connect
	//TODO
}else if($event == 2){//close
	//TODO
}else{
	if(FOOKING){
		$input = file_get_contents("php://input");
		$msg = unpackMsg($input);
		$type = $msg['type'];
		$data = $msg['data'];
	}else{
		$type = $_GET['type'];
		$data = $_GET;
	}
	runMsg($type, $data);
}

function runMsg($type, $data)
{
	$func = 'do' . $type;
	if(!function_exists($func)){
		echo "not found $func\n";
		return ;
	}

	$func($data);
}

//登陆
function doLogin($msg)
{
	$sid = SID;
	$redis = getRedis();
	
	//检测名称
	$name = trim(str_replace(array('<', '>'), array('&lt;', '&gt;'), $msg['name']));
	if(empty($name)){
		error("login", 1);
		return ;
	}
	
	$user = array(
		'sid' => $sid,
		'name' => $name,
		'time' => time(),
		'gold' => 100000,
	);
	$redis->hMSet("user:$sid", $user);
	
	success('login', $user);
}

//拉取房间列表
function doList($msg)
{
	$redis = getRedis();
	$tableKeys = $redis->keys('table:*');
	$list = array();
	foreach($tableKeys as $key){
		$tbl = $redis->hGetAll($key);
		$count = 0;
		
		//计数
		for($i = 1; $i <= SEAT_MAX; ++$i){
			if(isset($tbl["seat-$i"])){
				$seat = json_decode($tbl["seat-$i"], true);
				if(empty($seat['state'])){
					$count++;
				}
			}
		}
		
		$list[] = array(
			'tableid' => $tbl['tableid'],
			'title' => $tbl['title'],
			'mingold' => (int)$tbl['mingold'],
			'count' => $count,
			'state' => (int)$tbl['state'],
		);
	}
	
	success("list", $list);
}

//创建房间
function doCreate($msg)
{
	$sid = SID;
	$now = time();
	$redis = getRedis();
	$user = getUser();
	if(isset($user['tableid'])){
		return false;
	}
	
	//check gold
	if($user['gold'] < 10){
		error("create", 1, 'gold less');
		return false;
	}
	
	//make table id
	$tableid = $redis->incr('tableid');
	$seatid = 1;
	
	//update user info
	$redis->hmset("user:$sid", array(
		'tableid' => $tableid,
		'seatid' => 1,
	));
	
	//update table info
	$seats = array(
		$seatid => array(
			'sid' => $sid,
			'time' => $now,
			'seat' => $seatid,
		)
	);
	$redis->hmset("table:$tableid", array(
		'tableid' => $tableid,
		'master' => $sid,
		'title' => $msg['title'],
		'mingold' => (int)$msg['mingold'],
		'time' => $now,
		"seat-$seatid" => json_encode($seats[$seatid])
	));
	
	//加入到组
	$router = getRouter();
	$router->addChannel("table:$tableid", $sid);
	
	success("create", array(
		'tableid' => $tableid,
		'seatid' => $seatid,
		'seats' => $seats,
	));
}

//加入房间
function doJoin($msg)
{
	$sid = SID;
	$redis = getRedis();
	$tableid = (int)$msg['tableid'];
	
	if($tableid < 1 || $tableid > TABLE_MAX){
		return false;
	}
	
	//check table
	$table = getTable($tableid);
	if(empty($table)){
		return false;
	}
	
	//人数检测
	if(count($table['seats']) >= SEAT_MAX){
		error("join", 1, "seat full");
		return false;
	}
	
	//状态检测
	if(!empty($table['state'])){
		error("join", 2, "state error");
		return false;
	}
	
	//检测金币是否足够
	$user = getUser();
	if($user['gold'] < 10){
		error("join", 3, 'gold less');
		return false;
	}
	
	//检测是否已经加入房间
	if(isset($user['tableid'])){
		error("join", 4, 'table join failured');
		return false;
	}
	
	//更新玩家房间
	$ret = $redis->hsetnx("user:$sid", "tableid", $tableid);
	if(!$ret){
		error("join", 5, 'table join failured');
		return false;
	} 
	
	//确定位置
	$seatid = 0;
	for($i = 1; $i <= SEAT_MAX; ++$i){
		if(isset($table['seats'][$i])){
			continue;
		}
		
		//座位
		$seat = array(
			"sid" => $sid,
			"time" => time(),
			'seat' => $i,
		);
		
		//更新座位信息
		$ret = $redis->hsetnx("table:$tableid", "seat-$i", json_encode($seat));
		
		if($ret){
			$seatid = $i;
			$table['seats'][$seatid] = $seat;
			break;
		}
	}
	
	//特么座位上有刺么？座不下去。。
	if(!$seatid){
		$redis->hdel("user:$sid", "tableid");
		error("join", 6, 'update table info failured');
		return false;
	}
	
	//更新玩家座位
	$redis->hset("user:$sid", "seatid", $seatid);
	
	//单个用户加入消息
	$router = getRouter();
	$router->publish("table:$tableid", packMsg("joinEx", array(
		'tableid' => $tableid,
		'seatid' => $seatid,
		'user' => $user
	)));
	
	//加入到组
	$router->addChannel("table:$tableid", $sid);
	
	//玩家列表处理
	$seats = array();
	foreach($table['seats'] as $pos => $seat){
		$uid = $seat['sid'];
		
		if($uid == $sid){
			$u = $user;
			$u['tableid'] = $tableid;
			$u['seatid'] = $seatid;
		}else{
			$u = $redis->hgetAll("user:$uid");
		}
		
		$seats[$pos] = $u;
	}
	
	success("join", array(
		'tableid' => $tableid,
		'seatid' => $seatid,
		'seats' => $seats
	));
}

//退出房间
function doLeave($msg)
{
	$sid = SID;
	$redis = getRedis();
	$user = getUser();
	if(!$user['tableid']){
		return false;
	}
	
	$tableid = $user['tableid'];
	$seatid = $user['seatid'];
	$table = getTable($tableid);
	if(!empty($table['state'])){
		error("leave", 1);
		return false;
	}
	
	if($table['master'] == $sid){
		//转交管理权
		$master = null;
		foreach($table['seats'] as $seat){
			if($seat['sid'] != $sid && empty($seat['state'])){
				$master = $seat['sid'];
				break;
			}
		}
		
		if($master){
			//更新管理员
			$redis->hset("table:$tableid", "master", $master);
		}else{
			//全退出了，，删除房间
			$redis->del("table:$tableid");
		}
	}
	
	$redis->hdel("table:$tableid", "seat-$seatid");
	$redis->hdel("user:$sid", "tableid");
	$redis->hdel("user:$sid", "seatid");
	
	$router = getRouter();
	$router->publish("table:$tableid", packMsg("leave", array(
		'seatid' => $seatid
	)));
	$router->delChannel("table:$tableid", $sid);
	
	success("leave", true);
}

//开始
function doStart($msg)
{
	$sid = SID;
	$redis = getRedis();
	$user = getUser();
	if(empty($user['tableid'])){
		return false;
	}
	
	$tableid = $user['tableid'];
	$table = getTable($tableid);
	if($table['master'] != $sid){
		return false;
	}
	
	if(!empty($table['state'])){
		error("start", 1, "state error");
		return false;
	}
	
	if(count($table['seats']) < 2){
		error("start", 2, "player less");
		return false;
	}
	
	//give card
	$offset = 0;
	$update = array();
	$cards = makeCard();
	$current = 0;
	foreach($table['seats'] as $pos => $seat){
		$seat['state'] = 0;
		$seat['point'] = 2;
		$seat['cards'] = array_slice($cards, $offset, CARD_MAX);
		
		$offset+= CARD_MAX;
		
		//更新座位上的信息
		$update["seat-$pos"] = json_encode($seat);
		
		//房主最先出牌
		if($seat['sid'] == $sid){
			$current = $pos;
		}
	}
	
	$update['tableid'] = $tableid;
	$update['current'] = $current;
	$update['state'] = 1;
	$update['point'] = 2;
	$update['total'] = count($table['seats']) * 2;
	$redis->hmset("table:$tableid", $update);
	
	//通知所有人
	$router = getRouter();
	$router->sendAllMsg(packMsg("start", array(
		'tableid' => $tableid,
		'current' => $current,
	)));
}

//看牌
function doLook($msg)
{
	$sid = SID;
	$redis = getRedis();
	$user = getUser();
	if(empty($user['tableid'])){
		return false;
	}
	
	$tableid = $user['tableid'];
	$seatid = $user['seatid'];
	$table = getTable($tableid);
	if(empty($table['state'])){
		return false;
	}
	
	$seat = $table['seats'][$seatid];
	if(isset($seat['look'])){
		return false;
	}
	
	$seat['look'] = true;
	$redis->hset("table:$tableid", "seat-$seatid", json_encode($seat));
	
	$router = getRouter();
	$router->publish("table:$tableid", packMsg("lookEx", array(
		'sid' => $sid,
	)));
	
	success("look", array(
		'cards' => $seat['cards']
	));
}

//跟
function doFollow($msg)
{
	$sid = SID;
	$redis = getRedis();
	
	$user = getUser();
	if(empty($user['tableid'])){
		return false;
	}
	
	$tableid = $user['tableid'];
	$seatid = $user['seatid'];
	$seatkey = "seat-$seatid";
	
	//check table state
	$table = getTable($tableid);
	if(empty($table['state'])){
		return false;
	}
	
	//check seat state
	$seat = $table['seats'][$seatid];
	if($seat['state']){
		return false;
	}
	
	//check current
	if($table['current'] != $seatid){
		error("follow", 1);
		return false;
	}
	
	$needpoint = isset($seat['look']) ? $table['point'] * 2 : $table['point'];
	if($user['gold'] < $needpoint){
		error("follow", 2);
		return false;
	}
	
	//next
	$next = getNext($table);
	
	//update table
	$seat['point']+= $needpoint;
	$redis->hmset("table:$tableid", array(
		$seatkey => json_encode($seat),
		'current' => $next,
		'total' => $table['total'] + $needpoint,
	));
	
	$router = getRouter();
	$router->publish("table:$tableid", packMsg('follow', array(
		'sid' => $sid,
		'current' => $next,
		'point' => $needpoint,
		'total' => $table['total'] + $needpoint,
	)));
}

//加注
function doRise($msg)
{
	$sid = SID;
	$redis = getRedis();
	
	$user = getUser();
	if(!$user['tableid']){
		return false;
	}
	
	$tableid = $user['tableid'];
	$seatid = $user['seatid'];
	$seatkey = "seat-$seatid";
	
	//check table state
	$table = getTable($tableid);
	if(empty($table['state'])){
		return false;
	}
	
	//check seat state
	$seat = $table['seats'][$seatid];
	if($seat['state']){
		return false;
	}
	
	//check current
	if($seatid != $table['current']){
		error("rise", 1);
		return false;
	}
	
	$point = $table['point'] * 2;
	$needpoint = $seat['look'] ? $point * 2 : $point;
	if($user['gold'] < $needpoint){
		error("rise", 2);
		return false;
	}
	
	//next
	$next = getNext($table);
	
	//update talbe info
	$seat['point']+= $needpoint;
	$redis->hmset("table:$tableid", array(
		$seatkey => json_encode($seat),
		'current' => $next,
		'point' => $point,
		'total' => $table['total'] + $needpoint,
	));
	
	$router = getRouter();
	$router->publish("table:$tableid", packMsg('rise', array(
		'sid' => $sid,
		'current' => $next,
		'point' => $needpoint,
		'total' => $table['total'] + $needpoint,
	)));
}

//比牌
function doCompare($msg)
{
	$sid = SID;
	$redis = getRedis();
	$user = getUser();
	if(!$user['tableid']){
		return false;
	}
	
	$tableid = $user['tableid'];
	$seatid = $user['seatid'];
	$seatkey = "seat-$seatid";
	$eneid = $msg['seatid'];
	$enekey = "seat-$eneid";
	if($eneid == $seatid){
		return false;
	}
	
	//check table state
	$table = getTable($tableid);
	if(!$table['state']){
		error('compare', 1, 'table state error');
		return false;
	}
	
	//check seat state
	$seat = $table['seats'][$seatid];
	if(empty($seat) || $seat['state']){
		error('compare', 1, 'self state error');
		return false;
	}
	
	//check ene state
	$ene = $table['seats'][$eneid];
	if(empty($ene) || $ene['state']){
		error('compare', 1, 'target state error');
		return false;
	}
	
	//check current
	if($table['current'] != $seatid){
		error("compare", 1);
		return false;
	}
	
	//compare
	$update = array();
	$ret = compareCard($seat['cards'], $ene['cards']);
	if($ret <= 0){
		$winner = $ene['sid'];
		$seat['state'] = SEAT_STATE_LOSE;
		$table['seats'][$seatid] = $seat;
		$update[$seatkey] = json_encode($seat);
	}else{
		$winner = $sid;
		$ene['state'] = SEAT_STATE_LOSE;
		$table['seats'][$eneid] = $ene;
		$update[$enekey] = json_encode($ene);
	}
	
	//update table
	$next = getNext($table);
	$update['current'] = $next;
	$table['current'] = $next;
	$redis->hmset("table:$tableid", $update);
	
	$router = getRouter();
	$router->publish("table:$tableid", packMsg("compare", array(
		"sid" => $sid,
		"ene" => $ene['sid'],
		"winner" => $winner,
		"current" => $next,
	)));
	
	gameover($table);
}

//放弃
function doDiscard($msg)
{
	$sid = SID;
	$redis = getRedis();
	$user = getUser();
	if(!$user['tableid']){
		return false;
	}
	
	$tableid = $user['tableid'];
	$seatid = $user['seatid'];
	$seatkey = "seat-$seatid";
	
	//check table state
	$table = getTable();
	if(!$table['state']){
		return false;
	}
	
	//check state state
	$seat = $table['seats'][$seatid];
	if($seat['state']){
		return false;
	}
	
	//check current
	if($table['current'] != $seatid){
		return false;
	}
	
	
	//update seat
	$seat['state'] = SEAT_STATE_DISCARD;
	
	//next
	$next = getNext($table);
	
	$table['seats'][$seatid] = $seat;
	$table['current'] = $next;
	$redis->hmset("table:$tableid", array(
		$seatkey => json_encode($seat),
		'current' => $next,
	));
	
	$router = getRouter();
	$router->publish("table:$tableid", packMsg('discard', array(
		'sid' => $sid,
		'current' => $next,
	)));
	
	gameover($table);
}

function gameover($table)
{
	$winner = null;
	foreach($table['seats'] as $seat){
		if(empty($seat['state'])){
			if($winner){
				return ;
			}
			
			$winner = $seat['sid'];
		}
	}
	
	$redis = getRedis();
	$update = array();
	foreach($table['seats'] as $seat){
		$uid = $seat['sid'];
		if($uid == $winner){
			$v = $table["total"] - $seat['point'];
		}else{
			$v = -$seat['point'];
		}
		
		$redis->hIncrBy("user:$uid", "gold", $v);
		
		$update[$uid] = $v;
	}
	
	$tableid = $table['tableid'];
	$redis->hdel("table:$tableid", "state");
	
	$router = getRouter();
	$router->publish("table:$tableid", packMsg("over", array(
		"winner" => $winner,
		'update' => $update,
	)));
}

function unpackMsg($data)
{
	return json_decode($data, true);
}

function packMsg($type, $data, $error = 0)
{
	$msg = array(
		'type' => $type,
		'data' => $data
	);
	if($error){
		$msg['error'] = $error;
	}
	return json_encode($msg);
}

function error($type, $error, $msg = null)
{
	$body = packMsg($type, $msg, $error);
	if(FOOKING){
		header("Content-Length: " . strlen($body));
	}
	echo $body;
}

function success($type, $data)
{
	$body = packMsg($type, $data);
	if(FOOKING){
		header("Content-Length: " . strlen($body));
	}
	echo $body;
}

//获取当前登陆用户信息
function getUser()
{
	$redis = getRedis();
	$user = $redis->hGetAll("user:" . SID);
	return $user;
}

function getNext($table)
{
	$current = $old = $table['current'];
	do{
		if(++$current > SEAT_MAX){
			$current = 1;
		}
		
		if(empty($table['seats'][$current])){
			continue;
		}
		
		if(empty($table['seats'][$current]['state'])){
			break;
		}
	}while($old != $current);
	
	return $current;
}

//获取桌信息
function getTable($id)
{
	$redis = getRedis();
	$table = $redis->hgetAll("table:$id");
	
	//处理座位信息
	$seats = array();
	for($i = 1; $i <= SEAT_MAX; ++$i){
		$seatKey = "seat-$i";
		if(isset($table[$seatKey])){
			$seats[$i] = json_decode($table[$seatKey], true);
			unset($table[$seatKey]);
		}
	}
	
	$table['seats'] = $seats;
	
	return $table;
}

//获取redis
function getRedis()
{
	static $redis = null;
	if($redis === null){
		$redis = new Redis();
		$redis->connect(REDIS_HOST, REDIS_PORT);
	}
	return $redis;
}

//获取router
function getRouter()
{
	static $router = null;
	if($router === null){
		$router = new RouterClient(ROUTER_HOST, ROUTER_PORT);
	}
	return $router;
}

function makeCard()
{
	$cards = range(1, 52);
	shuffle($cards);
	return $cards;
}

function compareCard($c1, $c2)
{
	$info1 = getCardInfo($c1);
	$info2 = getCardInfo($c2);
	if($info1['type'] < $info2['type']){
		return -1;
	}else if($info1['type'] > $info2['type']){
		return 1;
	}else{
		if($info1['max'] < $info2['max']){
			return -1;
		}else if($info1['max'] > $info2['max']){
			return 1;
		}else{
			return $info1['min'] > $info2['min'] ? 1 : -1;
		}
	}
}

function getCardInfo($data)
{
	//转换
	$cards = array();
	foreach($data as $num){
		$card = $num % 13 ? $num % 13 : 13;
		$color = ceil($num / 13);
		$cards[] = array(
			'card' => $card,
			'color' => $color,
			'number' => $num
		);
	}
	
	//排序
	usort($cards, function($a, $b){
		if($a['card'] == $b['card']){
			return 0;
		}else if($a['card'] < $b['card']){
			return -1;
		}else{
			return 1;
		}
	});
	
	$len = count($cards);
	$sameNum = true;//同号
	$sameColor = true;//同色
	$order = true;//连号
	$pair = false;//对子
	$pairNum = null;//对子号
	$lastNum = 0;
	$lastColor = 0;
	$min = $max = 0;
	foreach($cards as $card){
		$color = $card['color'];
		$number = $card['card'];

		if($lastNum == 0){
			$lastNum = $number;
			$lastColor = $color;
			$min = $max = $number;
			continue;
		}
		
		if($number < $min){
			$min = $number;
		}
		
		if($number > $max){
			$max = $number;
		}
		
		if($sameNum && $number != $lastNum){
			$sameNum = false;
		}
		
		if($sameColor && $color != $lastColor){
			$sameColor = false;
		}
		
		if($order && $number != $lastNum + 1){
			$order = false;
		}
		
		if(!$pair && $number == $lastNum){
			$pair = true;
			$pairNum = $number;
		}
		
		$lastNum = $number;
		$lastColor = $color;
	}

	if($sameNum){//炸弹
		$type = 6;
		$min = $cards[0]['card'];
		$max = $cards[0]['card'];
	}else if($sameColor && $order){//同花顺
		$type = 5;
		$min = $cards[0]['card'];
		$max = $cards[$len - 1]['card'];
	}else if($sameColor){//同花
		$type = 4;
		$min = $cards[0]['card'];
		$max = $cards[$len - 1]['card'];
	}else if($order){//顺子
		$type = 3;
		$min = $cards[0]['card'];
		$max = $cards[$len - 1]['card'];
	}else if($pair){//对子
		$type = 2;
		$min = $pairNum == $min ? $max : $min;
		$max = $pairNum;
	}else{//单张
		$type = 1;
		$min = $cards[0]['card'];
		$max = $cards[$len - 1]['card'];
	}
	
	return array(
		'type' => $type,
		'min' => $min,
		'max' => $max,
		'cards' => $cards,
	);
}