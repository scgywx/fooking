--[[
fb是fooking buffer, 方法如下：
    fb.size(buffer)				返回buffer长度
	fb.data(buffer)				返回buffer数据(string)
	fb.append(buffer, "data")	向buffer追加数据
	fb.seek(buffer, 10)			标记buffer数据已读10字节
		如: buffer数据为abcdef，长度为6, 设置seek(buffer, 2),那么buffer内容为cdef,并且长度为4

fc是fooking connection, 方法如下：
    fc.buffer(conn)			返回conn的read buffer, 类型为lightuserdata
	fc.send(conn, "hello")	向conn发送数据
	fc.id(conn)				获取客户端session id
	fc.close(conn)			关闭连接

onRead与onWrite处理函数返回三种状态值，小于0, 等于0或大于0
大于0: 表示已经处理，数据由output返回
等于0: 表示继续由fooking按原生协议处理(size+body)处理，
小于0: 表示数据包不足，等待下次处理(output如果返回值小于0,则不返回任何数据到客户端)
]]

local fb = require("fooking.buffer");
local fc = require("fooking.connection");

dofile("Sha1.lua")

function onConnect(conn)
	print("new client, sid="..fc.id(conn))
end

function onClose(conn)
	print("close client, sid="..fc.id(conn))
end

function onRead(conn, requestid, input, output)
	local l = fb.size(input);
	local s = fb.data(input);
	
	--fc.send(conn, "HTTP/1.1 OK\r\nConnection: keep-alive\r\nContent-Length: 11\r\n\r\nhello world");
	
	if requestid == 0 then
		--握手协议
		local startpos = string.find(s, "Sec-WebSocket-Key: ", 1, true);
		if not startpos then
			print("not found Sec-WebSocket-Key");
			return -1;
		end
		
		local endpos = string.find(s, "\r\n", startpos + 19 , true);
		if not endpos then
			print("Sec-WebSocket-Key is not invalid");
			return -1;
		end
		
		local key = string.sub(s, startpos + 19, endpos - 1);
		local rkey = base64Encode(Sha1(key.."258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
		fc.send(conn, "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: WebSocket\r\nSec-WebSocket-Accept: "..rkey.."\r\n\r\n");
		
		fb.seek(input, l);
	else
		local total, body = wsDecode(s);
		if total < 0 then
			return -1;
		end
		
		fb.append(output, body);
		fb.seek(input, total);
	end
	
	return 1;
end

function onWrite(conn, requestid, input, output)
	local l = fb.size(input);
	local s = fb.data(input);
	
	fb.seek(input, l);
	local _, data = wsEncode(s);
	fc.send(conn, data);
	
	return 1;
end

function base64Encode(str)
	local b64chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
	local s64 = ''

	while #str > 0 do -- iterate through string
		local bytes_num = 0 -- number of shifted bytes
		local buf = 0 -- input buffer

		for byte_cnt=1,3 do
			buf = (buf * 256)
			if #str > 0 then -- if string not empty, shift 1st byte to buf
				buf = buf + string.byte(str, 1, 1)
				str = string.sub(str, 2)
				bytes_num = bytes_num + 1
			end
		end

		for group_cnt=1,(bytes_num+1) do
			b64char = math.fmod(math.floor(buf/262144), 64) + 1
			s64 = s64 .. string.sub(b64chars, b64char, b64char)
			buf = buf * 64
		end

		for fill_cnt=1,(3-bytes_num) do
			s64 = s64 .. '='
		end
	end

	return s64
end

function wsEncode(str)
	local data = "";
	
	--fin=1, rsv=000, opcode=0001
	local fin = 1 << 7;
	local rsv = 0;
	local opc = 1;
	data = data .. string.pack("B", fin | rsv | opc);
	
	--length
	local slen = #str;
	local mask = 0;
	if slen > 65535 then
		data = data .. string.pack("B", 127 | mask) .. string.pack(">J", slen);
	elseif slen > 125 then
		data = data .. string.pack("B", 126 | mask) .. string.pack(">H", slen);
	else
		data = data .. string.pack("B", slen | mask);
	end
	
	--mask
	--TODO
	
	--body
	data = data .. str;
	
	return #data, data;
end

function wsDecode(s)
	local total = #s;
	
	--检测长度
	if total < 4 then
		return -1;
	end
	
	--数据处理
	local c1,c2 = string.byte(s, 1, 2);
	local fin = c1 >> 7 & 0x1;
	local rsv1 = c1 >> 6 & 0x1;
	local rsv2 = c1 >> 5 & 0x1;
	local rsv3 = c1 >> 4 & 0x1;
	local opcode = c1 & 0xF;
	local hashmask = c2 >> 7 & 0x1;
	local length = c2 & 0x7F;
	local mask = "";
	
	
	if length == 126 then
		if hashmask == 1 then
			if total < 8 then
				return -1;
			end
			
			mask = string.sub(s, 5, 8);
		end
		
		local c3,c4 = string.byte(s, 3, 4);
		length = c3 << 8 | c4;
	elseif length == 127 then
		if hashmask == 1 then
			if total < 14 then
				return -1;
			end
			
			mask = string.sub(s, 11, 14);
		else
			if total < 10 then
				return -1;
			end
		end
		
		local c3,c4,c5,c6,c7,c8,c9,c10 = string.byte(s, 3, 10);
		length = (c3 << 56) | (c4 << 48) | (c5 << 40) | (c6 << 32) | 
				(c7 << 24) | (c8 << 16) | (c9 << 8) | c10;
	elseif hashmask == 1 then
		if total < 6 then
			return -1;
		end
		
		mask = string.sub(s, 3, 6);
	end
	
	local offset = 2;
	
	--length offset
	if length > 65535 then
		offset = offset + 8;
	elseif length > 125 then
		offset = offset + 2;
	end
	
	--mask offset
	if hashmask == 1 then
		offset = offset + 4;
	end
	
	--check length
	if offset + length < total then
		return -1;
	end
	
	local body = "";
	if hashmask == 1 then
		local m1,m2,m3,m4 = string.byte(mask, 1, 4);
		local marr = {m1, m2, m3, m4};
		
		for i=1,length do
			body = body .. string.char(string.byte(s, offset+i) ~ marr[(i-1)%4+1]);
		end
	else
		body = string.sub(s, offset, offset + length);
	end
	
	return offset+length, body;
end