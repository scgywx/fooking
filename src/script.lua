--fb是fooking buffer, 目前提供size, data, append, seek四个方法
--input与output处理函数返回三种状态值，小于0, 等于0或大于0
--小于0: 表示数据包不足，等待下次处理(ouput如果返回值小于0,则不返回任何数据到客户端)
--等于0: 表示继续由fooking按原协议处理(size+body)，
--大于0: 表示已经处理，数据由output返回

function inputHandler(input, output)
	local l = fb.size(input);
	local s = fb.data(input);
	fb.append(output, s);--将请求放入output
	fb.seek(input, l);--清空input
	return 1;
end

function outputHandler(input, output)
	local l = fb.size(input);
	local s = fb.data(input);
	fb.append(output, "HTTP/1.1 OK\r\nConnection: keep-alive\r\nContent-Length: ");
	fb.append(output, l);
	fb.append(output, "\r\n\r\n");
	fb.append(output, s);
	return 1;
end