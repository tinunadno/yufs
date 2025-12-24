#ifndef YUFS_PLATFORM_H
#define YUFS_PLATFORM_H

#ifdef __KERNEL__

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/types.h>

#define YUFS_MALLOC(sz) kmalloc(sz, GFP_KERNEL)
#define YUFS_FREE(ptr) kfree(ptr)
#define YUFS_MEMMOVE memmove
#define YUFS_MEMSET memset
#define YUFS_STRCMP strcmp
#define YUFS_STRLEN strlen
#define YUFS_STRCPY strcpy
#define YUFS_LOG_INFO_IMPL(fmt, ...) printk(KERN_INFO "YUFS: " fmt, ##__VA_ARGS__)
#define YUFS_LOG_ERR_IMPL(fmt, ...) printk(KERN_ERR "YUFS: " fmt, ##__VA_ARGS__)

#else // NOT_KERNEL :D

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>

typedef uint32_t umode_t;

#define YUFS_MALLOC(sz) malloc(sz)
#define YUFS_FREE(ptr) free(ptr)
#define YUFS_MEMMOVE memmove
#define YUFS_MEMSET memset
#define YUFS_STRCMP strcmp
#define YUFS_STRLEN strlen
#define YUFS_STRCPY strcpy
#define YUFS_LOG_INFO_IMPL(fmt, ...) printf("[INFO] YUFS: " fmt, ##__VA_ARGS__)
#define YUFS_LOG_ERR_IMPL(fmt, ...) printf("[ERR] YUFS: " fmt, ##__VA_ARGS__)

#ifndef S_IFMT
#define S_IFMT  00170000
#endif

#ifndef S_IFDIR
#define S_IFDIR 00040000
#endif

#ifndef S_IFREG
#define S_IFREG 00100000
#endif

#ifndef S_IRWXUGO
#define S_IRWXUGO 0777
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#endif

#ifdef ENABLE_LOG

#define YUFS_LOG_INFO(fmt, ...) YUFS_LOG_INFO_IMPL(fmt, ##__VA_ARGS__)
#define YUFS_LOG_ERR(fmt, ...) YUFS_LOG_INFO_IMPL(fmt, ##__VA_ARGS__)

#else

#define YUFS_LOG_INFO(fmt, ...) (void)0
#define YUFS_LOG_ERR(fmt, ...) (void)0

#endif

#endif // YUFS_PLATFORM_H