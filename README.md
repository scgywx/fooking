fooking is game gateway, read clients message transfer to backend(fpm request), then send message(fpm response) to clients.
it like nginx, so nginx + fpm = http server(php), fooking + fpm = socket server(php).

#features
1 it's dynamic gateway server add.
2 unique sessionid for per client.
3 group broadcast(like redis's pub/sub).
4 server status monitor.
5 client event notify(onconnect and onclose)
6 you can use any language(php, python, etc...)
7 custom message protocol by lua

#client protocol
client protocol is client to fooking's message protocol, use header(bigend 4 bytes) + body, 
but you can custom message protocol by lua.

#backend protocol
backend protocol is fooking to backend's message protocol, it use fastcgi, so you can any languages.

#getting started
step 1
   git clone https://github.com/scgywx/fooking.git
   cd {$FOOKING_PATH}
   make
step 2
   cd src
   ./fooking router.lua
step 3
   ./fooking config.lua

#arch
![image](http://static.oschina.net/uploads/space/2014/1209/222447_G7Ft_140911.jpg)
