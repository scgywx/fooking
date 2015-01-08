#fooking
![image](http://static.oschina.net/uploads/space/2014/1209/222447_G7Ft_140911.jpg)

#不是什么
1、不是框架   
2、不是扩展   
3、不是Http server   

#是什么
fooking是一个分布式游戏网关，主要用于承载长连接，将客户端的数据包完整的转发给后端，后端服务处理完之后由fooking转发给客户端。   
好像听起来有点像nginx+fpm？嗯！没错，如果是单纯的request/response，跟nginx类似;   
但在游戏中经常出现要主动推数据给客户端，而没有request，比如：A发消息给B，B是没有request的，只有response.   
嗯哼？就这些功能？听上也没什么吸引力啊。。   
当然不只如此，他包括：   
1、分布式网关配置,只需要简单配置就能动态添加网关,以提供更多的连接数量；   
2、SESSION维持，每个连接会有唯一sessionid,后端只需要指定sessionid发送消息即可，不用关心这个连接在哪台机器上；   
3、组播，N个用户加入到一组，只需要向组名发送消息即可，不用关心这个组有多少人(当然你非要自己去循环session发送我也阻止不了你)；   
4、服务器状态监控，可以观察到当前有多少组服务器，总共有多少连接，每台服务器上有多少连接，可以根据这些数据自定义一套规则来给客户端分配服务器；   
5、客户端连接与断开事件通知；   
6、后端无语言限制，遵循fastcgi协议即可;    
7、使用lua自定义前端协议

#优势
1、节约硬件，游戏通常刚开区压力比较大，过段时间人少了就没多少压了，配置多台服务器完全可以循环开服;   
2、后端无痛热更，例如php-fpm重启或者是热更代码，客户端完全没有察觉;  
3、开发方便，跟开发web一样，只需将要发送的数据直接输出即可(需要添加Content-Length用于确定包大小，详见协议说明)  
4、PHP错误能在log文件里一览无余,并且错误不会对fooking有任何影响   

#架构
fooking由一个router与多个gateway组成，所有gateway都会去连接router,后端主动推送的消息由router派发给gateway，然后由gateway转发客户端.   
request/response模式下不需要router干预,仅仅是gateway与backend(php-fpm)通信.   

#协议
网关为什么会有协议？既然是消息转发，就必须将一个包完整的发到后端，而不是让后端来检测包是否完整；   
协议分为两种，一种是前端协议，一种是后端协议   
前端协议是指客户端与fooking的交互协议，这个很简单，32位int + data(这是默认的协议，也可以使用lua进行自定义协议处理，详细参见script.lua, 需要配置SCRIPT_FILE，目前lua自定义协议功能比较弱，会慢慢完善).   
后端协议是使用Fastcgi，这就意味着，后端无所谓什么语言，只需要遵循fastcgi协议即可，我是phper,当然推荐使用fpm(使用hhvm也非常不错);    
注： 后端返回的数据必须有Content-Length标识返回数据长度，否则一律视为不返回数据到客户端，   
     另外数据是由后后向前切取，比如输出内容为abcdef,而Content-Length: 3,那么客户端会收到def..    

#编译
在fooking目录下执行make即可,启动需要cd src   

#配置
具体的配置请详见src/config.lua与src/router.lua

#启动router
./fooking router.lua

#启动gateway
./fooking config.lua

#example
已做了个简单的聊天室，位于example/chat  
使用方法：  
1、使用nginx或者apache将目录指向example/chat目录，并修改index.html的服务器IP与端口(需要访问index.html和chat.swf)   
2、运行python flash.py(flash的安全沙箱，因为客户端是使用flash socket)   
3、配置router.lua和config.lua，然后启动router和gateway   
4、访问localhost/index.html