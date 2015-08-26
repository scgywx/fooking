package main

import (
	"fmt"
	"net"
	"io/ioutil"
	"net/http"
	"net/url"
	"net/http/fcgi"
	"encoding/binary"
	"strings"
	"time"
	"strconv"
	"github.com/bitly/go-simplejson"
	"github.com/garyburd/redigo/redis"
)

//router对像
type RouterClient struct{
	_conn *net.TCPConn
}

//创建Router
func newRouter(host string) *RouterClient{
	addr, _ := net.ResolveTCPAddr("tcp4", host)
	conn, _ := net.DialTCP("tcp", nil, addr)
	
	return &RouterClient{
		_conn: conn,
	}
}

//指定用户发送消息
func (c *RouterClient) SendMsg(sid []byte, msg []byte){
	c.send(5, sid, msg)
}

//广播消息
func (c *RouterClient) SendAllMsg(msg []byte){
	c.send(6, []byte(""), msg)
}

//踢出用户
func (c *RouterClient) KickUser(sid []byte){
	c.send(4, sid, []byte(""))
}

//加入频道
func (c *RouterClient) JoinChannel(channel []byte, sid []byte){
	c.send(7, sid, channel);
}

//退出频道
func (c *RouterClient) LeaveChannel(channel []byte, sid []byte){
	c.send(8, sid, channel)
}

//发布消息到频道
func (c *RouterClient) Publish(channel []byte, msg []byte){
	c.send(9, channel, msg)
}

//服务器信息
func (c *RouterClient) Info(){
	//TODO
}

//发送信息
func (c *RouterClient) send(t uint16, sid []byte, msg []byte){
	binary.Write(c._conn, binary.BigEndian, t)
	binary.Write(c._conn, binary.BigEndian, uint16(len(sid)))
	binary.Write(c._conn, binary.BigEndian, uint32(len(msg)))
	c._conn.Write(sid)
	c._conn.Write(msg)
}

//接收数据
func (c *RouterClient) recv() []byte{
	b := make([]byte, 1024)
	_, _ = c._conn.Read(b);
	return b;
}

//聊天服务器
type ChatServer struct{
	_routerHost string
	_redisHost string
	_router *RouterClient
	_redis redis.Conn
}

//创建服务器
func newChatServer(routerHost string, redisHost string) *ChatServer{
	return &ChatServer{
		_routerHost: routerHost,
		_redisHost: redisHost,
	}
}

func (s *ChatServer) ServeHTTP(rep http.ResponseWriter, req *http.Request) {
	//短连接要调用一下这个，否则go不会主动断开连接
	//req.ParseForm();
	req.Form = make(url.Values);
	
	body, e := ioutil.ReadAll(req.Body)
	if e != nil {
		fmt.Printf("read request error\n")
	}else{
		sessionid := req.Header.Get("SESSIONID")
		event := req.Header.Get("EVENT")
		fmt.Printf("sid=%s, event=%s\n", sessionid, event);
		switch event {
			case "1"://新连接
				//TODO
			case "2"://连接关闭
				s.logout(sessionid)
			default://消息处理				
				if len(body) > 0 {
					js, err := simplejson.NewJson(body)
					if err != nil {
						fmt.Printf("parse JSON error, data=")
						fmt.Println(body)
					}else{
						r := s.handle(sessionid, js)
						if len(r) > 0 {
							rep.Header().Add("Content-Length", strconv.Itoa(len(r)))
							rep.Write(r)
						}else{
							fmt.Println("no message response")
						}
					}
				}
		}
	}
	
	//rep.Write([]byte("<h1>Hello World</h1>"))
}

//退出登陆
func (s *ChatServer) logout(sid string){
	rs := s.getRedis()
	name, _ := redis.String(rs.Do("HGET", "chatuser:"+sid, "name"))
	
	msg := simplejson.New()
	msg.Set("type", "close")
	msg.Set("id", sid)
	msg.Set("name", name)
	dat,_ := msg.Encode()
	
	rt := s.getRouter()
	rt.SendAllMsg(dat)
	
	rs.Do("DEL", "chatuser:"+sid)
}

//处理消息
func (s *ChatServer) handle(sid string, req *simplejson.Json) []byte{
	t, _ := req.Get("type").String()
	switch t {
		case "login": //登陆
			//发送登陆信息
			name, _ := req.Get("name").String()
			rt := s.getRouter()
			msg := simplejson.New()
			msg.Set("type", "login")
			msg.Set("id", sid)
			msg.Set("name", name)
			dat, _ := msg.Encode()
			rt.SendAllMsg(dat)
			
			//获取所有用户信息
			rs := s.getRedis()
			keys, _ := redis.Strings(rs.Do("KEYS", "chatuser:*"))
			var users []map[string]string
			for _, v := range keys {
				u, _ := redis.StringMap(rs.Do("HGETALL", v))
				users = append(users, u)
			}
			
			//设置当前用户信息
			rs.Do("HMSET", "chatuser:" + sid, "name", name, "id", sid)
			
			//返回用户列表
			rep := simplejson.New()
			rep.Set("type", "list")
			rep.Set("list", users)
			ret,_ := rep.Encode()
			
			return ret
		case "join": //加入房间
			rt := s.getRouter()
			channel, _ := req.Get("channel").Int()
			rt.JoinChannel([]byte("chat-channel-" + strconv.Itoa(channel)), []byte(sid))
		case "msg": //发送消息
			rs := s.getRedis()
			rt := s.getRouter()
			name, _ := redis.String(rs.Do("HGET", "chatuser:"+sid, "name"))
			text, _ := req.Get("text").String()
			channel, _ := req.Get("channel").Int()
			text = strings.Replace(text, "<", "&lt;", -1)
			text = strings.Replace(text, ">", "&gt;", -1)
			
			if channel > 0 {
				msg := simplejson.New()
				msg.Set("type", "msg")
				msg.Set("name", name)
				msg.Set("channel", strconv.Itoa(channel))
				msg.Set("text", text)
				msg.Set("time", time.Now().Format("15:04"))
				dat,_ := msg.Encode()
				
				fmt.Printf("sendToChannel channel=%d\n", channel)
				rt.Publish([]byte("chat-channel-" + strconv.Itoa(channel)), dat)
			}else{
				msg := simplejson.New()
				msg.Set("type", "msg")
				msg.Set("name", name)
				msg.Set("channel", "0")
				msg.Set("text", text)
				msg.Set("time", time.Now().Format("15:04"))
				dat,_ := msg.Encode()
				
				fmt.Println("sendToAll")
				rt.SendAllMsg(dat)
			}
		default:
			fmt.Printf("invalid type")
	}
	
	return []byte("")
}

//获取router
func (s *ChatServer) getRouter() *RouterClient{
	if s._router == nil {
		s._router = newRouter(s._routerHost);
	}
	
	return s._router;
}

//获取redis
func (s *ChatServer) getRedis() redis.Conn{
	if s._redis == nil {
		s._redis, _ = redis.Dial("tcp", s._redisHost)
	}
	
	return s._redis;
}

func main() {
	listener, _ := net.Listen("tcp", "0.0.0.0:9001")
	srv := newChatServer("test:9010", "test:6379");
	fcgi.Serve(listener, srv)
}