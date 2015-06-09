<?php
require('api.php');
date_default_timezone_set('Asia/Shanghai');
$data = file_get_contents("php://input");
$info = json_decode($data, true);
$router = new RouterClient('127.0.0.1', 9010);
$redis = new Redis();
$redis->connect('127.0.0.1', 6379);
$sid = $_SERVER['SESSIONID'];
if(empty($_SERVER['EVENT'])){
	if($info['type'] == 'login'){
		//发送登陆信息
		$info['name'] = str_replace(array('<', '>'), array('&lt;', '&gt;'), $info['name']);
		$router->sendAllMsg(json_encode(array(
			'type' => 'login',
			'name' => $info['name'],
			'id' => $sid,
		)));
		
		//获取所有用户信息
		$keys = $redis->keys('chatuser:*');
		$users = array();
		foreach($keys as $k){
			$u = $redis->hgetAll($k);
			$users[] = $u;
		}
		
		//设置当前用户信息
		$redis->hMset("chatuser:$sid", array(
			"name" => $info['name'],
			'id' => $sid,
		));
		
		//返回用户列表
		$body = json_encode(array(
			'type' => 'list',
			'list' => $users
		));
		header("Content-Length: " . strlen($body));
		echo $body;
	}else if($info['type'] == 'join'){
		//加入分组
		$router->addChannel('chat-channel-' . $info['channel'], $sid);
	}else if($info['type'] == 'msg'){
		//发送消息
		$name = $redis->hget("chatuser:$sid", "name");
		$info['text'] = str_replace(array('<','>'), array('&lt;','&gt;'), $info['text']);
		if($info['channel']){
			$router->publish('chat-channel-' . $info['channel'], json_encode(array(
				'type' => 'msg',
				'name' => $name,
				'channel' => (int)$info['channel'],
				'text' => $info['text'],
				'time' => date('i:s')
			)));
		}else{
			$router->sendAllMsg(json_encode(array(
				'type' => 'msg',
				'name' => $name,
				'channel' => 0,
				'text' => $info['text'],
				'time' => date('i:s')
			)));
		}
	}
}else if($_SERVER['EVENT'] == 2){
	//断开连接
	$user = $redis->hgetall("chatuser:$sid");
	if($user){
		$redis->del("chatuser:$sid");
		$router->sendAllMsg(json_encode(array(
			'type' => 'close',
			'name' => $user['name'],
			'id' => $sid,
		)));
	}
}