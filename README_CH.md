fooking是一个分布式网关服务器，主要用于承载客户端连接，将客户端请求转发到后端逻辑服务器，然后把逻辑服务器返回的结果转发到客户端.   
他类似Nginx，使用Nginx + FastCGI Server(如：FPM, etc..)构建Web服务器，同时可以使用Fooking + FastCGI Server(如：FPM, etc..)构建Socket服务器.   


# 特性
1 动态网关添加.   
2 每个客户端唯一SessionID.   
3 组播(类似redis的pub/sub).   
4 服务器状态监控.   
5 客户端事件通知(如：新连接、关闭连接).   
6 后端无语言限制(php, python, go, nodejs, etc...).   
7 自定义消息协议.   
8 后端长连接维持.   

# 客户端协议
这个是指客户端与fooking的通信协议，默认4字节数据大小(大端模式)和数据，同时你还可以使用Lua自定义协议。

# 后端协议
这是指fooking与后端逻辑服务器通信协议，这个使用FastCGI协议，后端可以使用任何语言来创建FastCGI服务器.    
这个协议非常简单，详见协议说明: http://www.fastcgi.com/drupal/node/6?q=node/22

# 使用说明
下面展示了fooking的使用，用例是聊天室，源代码位于example/chat目录下
* 第一步(下载和编译)   
   git clone https://github.com/scgywx/fooking.git   
   cd {$FOOKING_PATH}   
   make   
* 第二步(启动Router)   
   cd src   
   ./fooking ../router.lua   
* 第三步(启动Gateway)   
   ./fooking ../config.lua   
* 第四步(启动FastCGI服务器, 如：fpm)
   service php-fpm start(如果已经启动，请忽略此步骤)
* 第五步(测试)
   修改example/chat/index.html文件的Websocket的服务器IP和端口(查找ws://)    
   然后用浏览器打开index.html即可       

# 架构图
![image](http://static.oschina.net/uploads/space/2014/1209/222447_G7Ft_140911.jpg)