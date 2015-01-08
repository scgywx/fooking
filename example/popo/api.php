<?php
class RouterClient
{
	private $_host = null;
	private $_port = null;
	private $_socket = null;
	
	public function __construct($host, $port)
	{
		$this->_host = $host;
		$this->_port = $port;
		
		$this->_socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
		if(!socket_connect($this->_socket, $this->_host, $this->_port)){
			close();
			$this->_socket = false;
		}
	}
	
	public function close()
	{
		if($this->_socket){
			socket_close($this->_socket);
		}
	}
	
	public function sendMsg($sid, $msg)
	{
		$this->send(5, $sid, $msg);
	}
	
	public function sendAllMsg($msg)
	{
		$this->send(6, "", $msg);
	}

	public function kickUser($sid)
	{
		$this->send(4, $sid, "");
	}
	
	public function addChannel($channel, $sid)
	{
		$this->send(7, $sid, $channel);
	}
	
	public function delChannel($channel, $sid)
	{
		$this->send(8, $sid, $channel);
	}
	
	public function publish($channel, $msg)
	{
		$this->send(9, $channel, $msg);
	}
	
	
	public function info()
	{
		$this->send(12, "", "");
		
		$msg = $this->recv();
		if(!$msg){
			echo "read info data error";
			return array();
		}
		
		$data = $msg['body'];
		$clients = $this->readInt32($data);
		$gateways = $this->readInt32($data, 4);
		$list = array();
		
		$offset = 8;
		for($i = 0; $i < $gateways; ++$i){
			$id = $this->readInt32($data, $offset);
			$num = $this->readInt32($data, $offset + 4);
			$offset+= 8;
			$list[$id][] = array(
				'num' => $num
			);
		}
		
		return array(
			'clients' => $clients,
			'gateways' => $gateways,
			'list' => $list
		);
	}
	
	private function send($type, $sid, $msg)
	{
		if ($this->_socket === false) {
			echo "socket error\n";
			return false;
		}

		$data = $this->writeInt16($type);
		$data.= $this->writeInt16(strlen($sid));
		$data.= $this->writeInt32(strlen($msg));
		$data.= $sid;
		$data.= $msg;

		socket_write($this->_socket, $data, strlen($data));
	}
	
	private function recv()
	{
		$data = socket_read($this->_socket, 8);
		if(strlen($data) != 8){
			return false;
		}
		
		$type = $this->readInt16($data);
		$slen = $this->readInt16($data, 2);
		$dlen = $this->readInt32($data, 4);
		$body = socket_read($this->_socket, $dlen);
		
		return array(
			'type' => $type,
			'slen' => $slen,
			'len' => $dlen,
			'body' => $body
		);
	}
	
	private function writeInt16($val)
	{
		return (chr(($val >> 8) & 0xFF) . chr($val & 0xFF));
	}
	
	private function writeInt32($val)
	{
		return (chr(($val >> 24) & 0xFF) . chr(($val >> 16) & 0xFF) . chr(($val >> 8) & 0xFF) . chr($val & 0xFF));
	}
	
	private function readInt16($data, $offset = 0)
	{
		return (ord($data{$offset+0}) << 8 | ord($data{$offset+1}));
	}
	
	private function readInt32($data, $offset = 0)
	{
		return (ord($data{$offset+0}) << 24) | (ord($data{$offset+1}) << 16) | (ord($data{$offset+2}) << 8) | ord($data{$offset+3});
	}
}