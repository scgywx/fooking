fooking is game gateway.It transfers client's requests to backend and send responses back with fpm protocol.
Just like Nginx, as building a http server with nginx and fpm, you can create a socket server with fooking.    

# features
1 gateway server adding dynamicly.
2 unique sessionid for each client.   
3 group broadcast(like redis's pub/sub).   
4 server status monitor.   
5 client's event notify(onconnect and onclose).   
6 all language support(php, python, etc...).   
7 custom message protocol by lua.   

# client protocol
client protocol is the protocol use in clients to fooking, build up with 4 bytes header in bigend and body,and you can custom protocol with lua. 

# backend protocol
backend protocol is the protocol use in fooking to backends, you can use any luanguage with support fastcgi.

# getting started
step 1   
   git clone https://github.com/scgywx/fooking.git   
   cd {$FOOKING_PATH}   
   make   
step 2   
   cd src   
   ./fooking router.lua   
step 3   
   ./fooking config.lua   

# arch
![image](http://static.oschina.net/uploads/space/2014/1209/222447_G7Ft_140911.jpg)
