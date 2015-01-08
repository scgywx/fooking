<?php
require('api.php');
date_default_timezone_set('Asia/Shanghai');
$data = file_get_contents("php://input");
$msg = json_decode($data, true);

if($_SERVER['EVENT'] == 2){//close
	$client = new RouterClient('127.0.0.1', 9010);
	$client->sendAllMsg(json_encode(array(
		'type' => 'leave',
		'id' => $_SERVER['SESSIONID']
	)));
	$redis = new Redis();
	$redis->connect('localhost', 6379);
	$key = 'role:' . $_SERVER['SESSIONID'];
	$redis->del($key);
}else{
	if($msg['type'] == 'start'){
		$client = new RouterClient('127.0.0.1', 9010);
		$client->sendAllMsg($data);
		
		$redis = new Redis();
		$sid = $_SERVER['SESSIONID'];
		$key = 'role:' . $_SERVER['SESSIONID'];
		$redis->connect('localhost', 6379);
		$redis->hMset($key, array(
			'x' => $msg['x'],
			'y' => $msg['y'],
		));
		
		echo "send start";
	}else if($msg['type'] == 'stop'){
		$client = new RouterClient('127.0.0.1', 9010);
		$client->sendAllMsg($data);
		
		$redis = new Redis();
		$sid = $_SERVER['SESSIONID'];
		$key = 'role:' . $_SERVER['SESSIONID'];
		$redis->connect('localhost', 6379);
		$redis->hMset($key, array(
			'x' => $msg['x'],
			'y' => $msg['y'],
		));
		
		echo "send stop";
	}else if($msg['type'] == 'join'){
		echo 'join';
		$client = new RouterClient('127.0.0.1', 9010);
		$sid = $_SERVER['SESSIONID'];
		$msg['id'] = $sid;
		$msg['x'] = mt_rand(0, 600 - 48);
		$msg['y'] = mt_rand(0, 520 - 64);
		
		//write to db
		$redis = new Redis();
		
		$key = 'role:' . $sid;
		$redis->connect('localhost', 6379);
		$redis->hMset($key, array(
			'id' => $msg['id'],
			'x' => $msg['x'],
			'y' => $msg['y'],
			'name' => $msg['name']
		));
		
		//join ok
		$client->sendMsg($sid, json_encode(array(
			'type' => 'joinok',
			'id' => $sid,
			'name' => $msg['name']
		)));
		
		//sync
		$keys = $redis->keys('role:*');
		$list = array();
		foreach($keys as $k){
			$data = $redis->hgetAll($k);
			if($k == $key) continue;
			$data['type'] = 'join';
			$client->sendMsg($sid, json_encode($data));
			echo "sync-->" . $data['id'] . "\n";
		}
		
		//send all
		$client->sendAllMsg(json_encode($msg));
	}else if($msg['type'] == 'popo'){
		$redis = new Redis();
		$redis->connect('localhost', 6379);
		$popid = $redis->incr('popoid');
		$msg['id'] = 'p' . $popid;
		$client = new RouterClient('127.0.0.1', 9010);
		$client->sendAllMsg(json_encode($msg));
	}else if($msg['type'] == 'boom'){
		$client = new RouterClient('127.0.0.1', 9010);
		$client->sendAllMsg($data);
	}else if($msg['type'] == 'message'){
		//$msg['text'] = mb_substr($msg['text'], 0, 10);
		$client = new RouterClient('127.0.0.1', 9010);
		$client->sendAllMsg($data);
	}else if($msg['type'] == 'kill'){
		$redis = new Redis();
		$key = 'role:' . $msg['id'];
		$redis->connect('localhost', 6379);
		$redis->hset($key, 'die', 1);
	}
}