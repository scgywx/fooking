#pragma once
#include <string>
#include <ext/hash_map>
#include "fooking.h"
#include "Connection.h"
#include "Backend.h"

using namespace __gnu_cxx;

namespace __gnu_cxx
{
	inline unsigned int generic_hash_func(const unsigned char *buf, int len)
	{
		unsigned int h = 5381;

		while(len--)
			h = ((h << 5) + h) + (*buf++); /* hash * 33 + c */
			
		return h;
	}

	template<>
	struct hash<std::string>
	{
		size_t operator()(const std::string& s) const
		{
			return generic_hash_func((const unsigned char*)s.c_str(), s.size());
		}
	};
	
	template<>
	struct hash<NS_NAME::Connection*>
	{
		size_t operator()(NS_NAME::Connection* const &p) const
		{
			return generic_hash_func((const unsigned char*)&p, sizeof(NS_NAME::Connection*));
		}
	};
	
	template<>
	struct hash<NS_NAME::Backend*>
	{
		size_t operator()(NS_NAME::Backend* const &p) const
		{
			return generic_hash_func((const unsigned char*)&p, sizeof(NS_NAME::Backend*));
		}
	};
}