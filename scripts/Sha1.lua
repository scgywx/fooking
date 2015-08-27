-------------------------------------------------
---      *** SHA-1 algorithm for Lua ***      ---
-------------------------------------------------
--- Author:  Martin Huesser                   ---
--- Date:    2008-06-16                       ---
--- License: You may use this code in your    ---
---          projects as long as this header  ---
---          stays intact.                    ---
-------------------------------------------------
local string  = require('string');
local strlen  = string.len
local strchar = string.char
local strbyte = string.byte
local strsub  = string.sub
local floor   = math.floor
local h0, h1, h2, h3, h4
local INT_MAX = 4294967295

-------------------------------------------------

local function LeftRotate(val, nr)
	val = val & INT_MAX;
	local v2 = (((val << nr) & INT_MAX) + ((val >> (32 - nr)) & INT_MAX)) & INT_MAX;
	return v2;
end

-------------------------------------------------

local function PreProcess(str)
	local bitlen, i
	local str2 = ""
	bitlen = #str * 8
	str = str .. string.char(128)
	i = 56 - (#str & 63)
	if (i < 0) then
		i = i + 64
	end
	str = str .. string.rep(string.char(0), i);
	for i = 1, 8 do
		str2 = string.char((bitlen & 255)) .. str2
		bitlen = floor(bitlen / 256)
	end
	return str .. str2
end

-------------------------------------------------

local function MainLoop(str)
	local a, b, c, d, e, f, k, t
	local i, j
	local w = {}
	while #str > 0 do
		for i = 0, 15 do
			w[i] = 0
			for j = 1, 4 do
				w[i] = w[i] * 256 + strbyte(str, i * 4 + j)
			end
		end
		for i = 16, 79 do
			w[i] = LeftRotate(((w[i - 3] ~ w[i - 8]) ~ (w[i - 14]~ w[i - 16])), 1)
		end
		a = h0
		b = h1
		c = h2
		d = h3
		e = h4
		for i = 0, 79 do
			if (i < 20) then
				f = ((b & c) | (~(b) & d))
				k = 1518500249
			elseif (i < 40) then
				f = ((b ~ c) ~ d)
				k = 1859775393
			elseif (i < 60) then
				f = (((b & c) | (b & d)) | (c & d))
				k = 2400959708
			else
				f = ((b ~ c) ~ d)
				k = 3395469782
			end
			t = LeftRotate(a, 5) + f + e + k + w[i]	
			e = d
			d = c
			c = LeftRotate(b, 30)
			b = a
			a = t
		end
		h0 = (h0 + a) & INT_MAX
		h1 = (h1 + b) & INT_MAX
		h2 = (h2 + c) & INT_MAX
		h3 = (h3 + d) & INT_MAX
		h4 = (h4 + e) & INT_MAX
		str = strsub(str, 65)
	end
end

-------------------------------------------------

function Sha1(str)
	str = PreProcess(str)
	h0  = 1732584193
	h1  = 4023233417
	h2  = 2562383102
	h3  = 0271733878
	h4  = 3285377520
	MainLoop(str)
	return string.pack(">IIIII", h0, h1, h2, h3, h4);
end