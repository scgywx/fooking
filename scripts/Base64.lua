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