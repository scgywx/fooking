#pragma once

//gcc
#ifdef __x86_64__
#define ATOMIC_T_LEN					(sizeof("-9223372036854775808") - 1)
#else
#define ATOMIC_T_LEN					(sizeof("-2147483648") - 1)
#endif

typedef long                        	atomic_int_t;
typedef unsigned long               	atomic_uint_t;
typedef volatile atomic_uint_t  		atomic_t;

#define atomic_cmp_set(lock, old, set)	__sync_bool_compare_and_swap(lock, old, set)
#define atomic_fetch_add(value, add)	__sync_fetch_and_add(value, add)
#define atomic_fetch_sub(value, sub)	__sync_fetch_and_sub(value, sub)
#define memory_barrier()				__sync_synchronize()