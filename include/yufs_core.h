#ifndef YUFS_YUFS_CORE_H
#define YUFS_YUFS_CORE_H

#include "yufs_platform.h"

struct yufs_stat
{
    uint32_t id;
    umode_t mode;
    uint64_t size;
};

struct yufs_dirent
{
    uint32_t id;
    char name[256];
    umode_t type;
};
typedef int (*yufs_filldir_y)(void* ctx, const char* name, int name_len, uint32_t id, umode_t type);


[[nodiscard]] int     yufs_core_init(void);
void                  yufs_core_destroy(void);
[[nodiscard]] int     yufs_core_lookup(uint32_t parent_id, const char* name, struct yufs_stat* result);
[[nodiscard]] int     yufs_core_create(uint32_t parent_id, const char* name, umode_t mode, struct yufs_stat* result);
[[nodiscard]] int     yufs_core_mkdir(uint32_t parent_id, const char* name, umode_t mode, struct yufs_stat* result);
[[nodiscard]] int     yufs_core_unlink(uint32_t parent_id, const char* name);
[[nodiscard]] int     yufs_core_rmdir(uint32_t parent_id, const char* name);
[[nodiscard]] int     yufs_core_rmdir(uint32_t parent_id, const char* name);
[[nodiscard]] int     yufs_core_getattr(uint32_t parent_id, const char* name);
[[nodiscard]] int     yufs_core_read(uint32_t parent_id, const char* name);
[[nodiscard]] int     yufs_core_write(uint32_t parent_id, const char* name);
[[nodiscard]] int     yufs_core_iterate(uint32_t id, yufs_filldir_y callback, void* ctx);

#endif //YUFS_YUFS_CORE_H
