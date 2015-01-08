<?php
require('api.php');
date_default_timezone_set('Asia/Shanghai');
$data = file_get_contents("php://input");
$info = json_decode($data, true);
if($info['type'] == 'msg'){
	$client = new RouterClient('127.0.0.1', 9010);
	$client->sendAllMsg($info['name'] . ' : ' . $info['text']);
}else if($info['type'] == 'join'){
	$client = new RouterClient('127.0.0.1', 9010);
	$client->addChannel($info['name'], $_SERVER['SESSIONID']);
}else if($info['type'] == 'leave'){
	$client = new RouterClient('127.0.0.1', 9010);
	$client->delChannel($info['name'], $_SERVER['SESSIONID']);
}else if($info['type'] == 'group'){
	$client = new RouterClient('127.0.0.1', 9010);
	$client->publish('test', $info['name'] . '[group] : ' . $info['text']);
}