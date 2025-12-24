#ifndef YUFS_YUFSCore_H
#define YUFS_YUFSCore_H

#include "yufs_platform.h"

#define MAX_NAME_SIZE 256

struct YUFS_stat
{
    uint32_t id;
    umode_t mode;
    uint64_t size;
};

struct YUFS_dirent
{
    uint32_t id;
    char name[MAX_NAME_SIZE];
    umode_t type;
};
typedef int (*yufs_filldir_y)(void* ctx, const char* name, int name_len, uint32_t id, umode_t type);


[[nodiscard]] int     YUFSCore_init(void);
void                  YUFSCore_destroy(void);
[[nodiscard]] int     YUFSCore_lookup(uint32_t parent_id, const char* name, struct YUFS_stat* result);
[[nodiscard]] int     YUFSCore_create(uint32_t parent_id, const char* name, umode_t mode, struct YUFS_stat* result);
[[nodiscard]] int     YUFSCore_mkdir(uint32_t parent_id, const char* name, umode_t mode, struct YUFS_stat* result);
[[nodiscard]] int     YUFSCore_unlink(uint32_t parent_id, const char* name);
[[nodiscard]] int     YUFSCore_rmdir(uint32_t parent_id, const char* name);
[[nodiscard]] int     YUFSCore_getattr(uint32_t id, struct YUFS_stat* result);
[[nodiscard]] int     YUFSCore_read(uint32_t id, char *buf, size_t size, loff_t offset);
[[nodiscard]] int     YUFSCore_write(uint32_t id, const char *buf, size_t size, loff_t offset);
[[nodiscard]] int     YUFSCore_iterate(uint32_t id, yufs_filldir_y callback, void* ctx);

#endif //YUFS_YUFSCore_H
