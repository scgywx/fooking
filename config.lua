--监听IP和端口
HOST = "0.0.0.0";
PORT = 9005;

--守护进行
--DAEMONIZE = 1;

--日志文件(为空则不输出日志， stdout-输出到标准输出上, 其它则按文件处理)
LOG_FILE = "stdout";--直接屏幕打印
--LOG_FILE = "/tmp/fooking-gateway.log";--输出到文件

--1: 仅error
--2: error与info
--3: 所有
LOG_LEVEL = 3;

--是否路由服务器(0-否, 1-是)
--如果是路由，ROUTER以下的配置将被忽略)
ROUTER = 0;

-----------------------万万没想到，我最终还是成为了分割线！-----------------------
--服务器ID
SERVER_ID = 1;

--工作进程
WORKER_NUM = 2;

--最大连接数
MAX_CLIENT_NUM = 10000;

--发送缓冲区
SEND_BUFF_SIZE = 8192;

--接收缓冲区
RECV_BUFF_SIZE = 8192;

--路由服务器
ROUTER_HOST = "127.0.0.1";
ROUTER_PORT = 9010;

--指定时间内连接没有数据包请求，将会踢掉连接(单位秒),为0不处理
IDLE_TIME = 0;

--脚本
SCRIPT_FILE = "../scripts/Websocket.lua";

--后端服务器列表
BACKEND_CONNECT_TIMEOUT = 5;--连接超时时间(单位秒)
BACKEND_READ_TIMEOUT = 10;--数据接收超时间(单位秒)
BACKEND_KEEPALIVE = 0;--最大维持长连接数量
BACKEND_SERVER = {
	["127.0.0.1:9000"] = 5,--第一列是socket选项，第二列是权重(跟nginx的upstream差不多一个意思)
};

--新连接是否通知(0-不通知, 1-通知)
--请求头会有EVENT=1
EVENT_CONNECT = 0;

--关闭连接是否通知(0-不通知, 1-通知)
--请求头会有EVENT=2
EVENT_CLOSE = 1;

--fastcgi params
FASTCGI_PREFIX = "";--go下要使用HTTP_作为前缀，否则拿不了SESSIONID和EVENT
FASTCGI_ROOT = "/home/fooking/example/chat/";
FASTCGI_FILE = "gateway.php";
FASTCGI_PARAMS = {
	["SERVER_SOFTWARE"] = "fooking",
	["SERVER_PROTOCOL"] = "HTTP/1.1",
	["GATEWAY_INTERFACE"] = "CGI/1.1",
	["REQUEST_METHOD"] = "POST",
	["SCRIPT_FILENAME"] = FASTCGI_ROOT..FASTCGI_FILE,
	["SCRIPT_NAME"] = FASTCGI_FILE,
	["DOCUMENT_ROOT"] = FASTCGI_ROOT,
	["SERVER_NAME"] = "www.myhost.com",
	["QUERY_STRING"] = "a=10&b=20",
};
