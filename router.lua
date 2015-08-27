--监听IP和端口
HOST = "0.0.0.0";
PORT = 9010;

--守护进行
--DAEMONIZE = 1;

--日志文件(为空则不输出日志， stdout-输出到标准输出上, 其它则按文件处理)
LOG_FILE = "stdout";--直接屏幕打印
--LOG_FILE = "/tmp/fooking-router.log";--输出到文件

--1: 仅error
--2: error与info
--3: 所有
LOG_LEVEL = 3;

--是否路由服务器(0-否, 1-是)
--如果是路由，ROUTER以下的配置将被忽略)
ROUTER = 1;