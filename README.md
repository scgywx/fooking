fooking is distributed gateway server.It transfers client's requests to backend and send responses back with fpm protocol.  
Just like Nginx, as building a http server with nginx and fastcgi server(e.g fpm, etc..), you can create a socket server with fooking.    

# features
1 gateway server adding dynamicly.   
2 unique sessionid for each client.   
3 group broadcast(like redis's pub/sub).   
4 server status monitor.   
5 clients event notify(onconnect and onclose).   
6 all language supported(php, python, etc...).   
7 custom message protocol by lua.   
8 backend connection keepalive.   

# client protocol
client protocol is the protocol use in clients to fooking,    
default build up with 4 bytes header in bigend and body, But you can custom protocol with lua(reference script.lua).  

# backend protocol
backend protocol is the protocol use in fooking to backends, you can use any luanguage with support fastcgi.  
this protocol is simply, reference: http://www.fastcgi.com/drupal/node/6?q=node/22    

# getting started
this example is chat room, source code in example/chat   
* Step 1(download and compile)   
   git clone https://github.com/scgywx/fooking.git   
   cd {$FOOKING_PATH}   
   make   
* Step 2(start fooking router server)   
   cd src   
   ./fooking ../router.lua   
* Step 3(start fooking gateway server)   
   ./fooking ../config.lua   
* Step 4(start fastcgi server, e.g php-fpm)
   service php-fpm start(if it was started please skip this step)
* Step 5(test)
   modify websocket server host and port in example/chat/index.html(search 'ws://')   
   open index.html in your browser and starting chat   

# arch
![image](http://static.oschina.net/uploads/space/2014/1209/222447_G7Ft_140911.jpg)

