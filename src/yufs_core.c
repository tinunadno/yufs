#include "yufs_core.h"

#ifndef __RAM_VERSION__
#ifndef __WEB_VERSION__
#error "you have to define __WEB_VERSION__ or __RAM_VERSION__, now none of them are defined!"
#endif
#endif

#ifdef __RAM_VERSION__
#ifdef __WEB_VERSION__
#error "you can't build yufs with __WEB_VERSION__ and __RAM_VERSION__ both! remove one of them"
#endif
#endif

#ifdef __RAM_VERSION__

#define MAX_FILES 1024
#define ROOT_INO 1000

struct YUFS_Inode {
    uint32_t id;
    umode_t mode;
    int nlink;
    char* content;
    size_t size;
    struct YUFS_Dirent* main_dentry;
};


struct YUFS_Dirent {
    char name[MAX_NAME_SIZE];
    uint32_t inode_id;
    struct YUFS_Dirent* parent;
    struct YUFS_Dirent* first_child;
    struct YUFS_Dirent* next_sibling;
    struct YUFS_Dirent* prev_sibling;
};


static struct YUFS_Inode* inodeTable[MAX_FILES];

static struct YUFS_Inode* allocInode(void) {
    for (size_t i = 1; i < MAX_FILES; i++) {
        if (inodeTable[i] == NULL) {
            struct YUFS_Inode* node = (struct YUFS_Inode*)YUFS_MALLOC(sizeof(struct YUFS_Inode));
            if (!node) return NULL;
            YUFS_MEMSET(node, 0, sizeof(struct YUFS_Inode));
            node->id = i;
            node->nlink = 1;
            inodeTable[i] = node;
            YUFS_LOG_INFO("allocated node with id %d", node->id);
            return node;
        }
    }
    YUFS_LOG_INFO("failed to allocate node");
    return NULL;
}

static struct YUFS_Dirent* allocDirent(const char* name, uint32_t inode_id) {
    struct YUFS_Dirent* d = (struct YUFS_Dirent*)YUFS_MALLOC(sizeof(struct YUFS_Dirent));
    if (!d) {
        YUFS_LOG_INFO("failed to allocate dirent");
        return NULL;
    }
    YUFS_MEMSET(d, 0, sizeof(struct YUFS_Dirent));
    YUFS_STRCPY(d->name, name);
    d->inode_id = inode_id;
    YUFS_LOG_INFO("allocated dirent for node with id %d", inode_id);
    return d;
}

static void freeInode(struct YUFS_Inode* node) {
    YUFS_LOG_INFO("freed node with id %d", node->id);
    if (node->content) YUFS_FREE(node->content);
    inodeTable[node->id] = NULL;
    YUFS_FREE(node);
}

static void* yu_realloc(void* old, size_t oldSz, size_t newSz) {
    if (newSz == 0) {
        if (old) YUFS_FREE(old);
        return NULL;
    }
    void* ret = YUFS_MALLOC(newSz);
    if (!ret) return NULL;
    if (old) {
        size_t copySz = (oldSz < newSz) ? oldSz : newSz;
        YUFS_MEMMOVE(ret, old, copySz);
        YUFS_FREE(old);
    }
    return ret;
}

int YUFSCore_init(void) {
    YUFS_MEMSET(inodeTable, 0, sizeof(inodeTable));
    struct YUFS_Inode* rootInode = allocInode();
    if (!rootInode) return -1;
    rootInode->id = ROOT_INO;

    if (inodeTable[1]) { inodeTable[1] = NULL; inodeTable[ROOT_INO] = rootInode; }

    rootInode->mode = S_IFDIR | 0777;

    struct YUFS_Dirent* rootDirent = allocDirent("", ROOT_INO);
    if (!rootDirent) return -1;

    rootDirent->parent = rootDirent;
    rootInode->main_dentry = rootDirent;

    return 0;
}

void YUFSCore_destroy(void) {
    for (size_t i = 0; i < MAX_FILES; i++) {
        if (inodeTable[i]) freeInode(inodeTable[i]);
    }
}

static struct YUFS_Dirent* find_child(struct YUFS_Dirent* parent, const char* name) {
    struct YUFS_Dirent* child = parent->first_child;
    while (child) {
        if (YUFS_STRCMP(child->name, name) == 0) return child;
        child = child->next_sibling;
    }
    return NULL;
}

int YUFSCore_lookup(const char*, uint32_t parent_id, const char* name, struct YUFS_stat* result) {
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;
    struct YUFS_Inode* parentNode = inodeTable[parent_id];

    if (!S_ISDIR(parentNode->mode) || !parentNode->main_dentry) return -1;

    struct YUFS_Dirent* child = find_child(parentNode->main_dentry, name);
    if (!child) return -1;

    struct YUFS_Inode* inode = inodeTable[child->inode_id];
    if (!inode) return -1;

    result->id = inode->id;
    result->mode = inode->mode;
    result->size = inode->size;
    YUFS_LOG_INFO("lookup for parent id %d and name %s succeed", parent_id, name);
    return 0;
}

static void attach_dentry(struct YUFS_Dirent* parent, struct YUFS_Dirent* child) {
    child->parent = parent;
    child->next_sibling = parent->first_child;
    if (parent->first_child) {
        parent->first_child->prev_sibling = child;
    }
    parent->first_child = child;
}

int YUFSCore_create(const char*, uint32_t parent_id, const char* name, umode_t mode, struct YUFS_stat* result) {
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;
    struct YUFS_Inode* parentInode = inodeTable[parent_id];
    if (!S_ISDIR(parentInode->mode)) return -1;

    struct YUFS_Inode* newInode = allocInode();
    if (!newInode) return -1;
    newInode->mode = mode;

    struct YUFS_Dirent* newDirent = allocDirent(name, newInode->id);
    if (!newDirent) { freeInode(newInode); return -1; }

    if (S_ISDIR(mode)) newInode->main_dentry = newDirent;

    attach_dentry(parentInode->main_dentry, newDirent);

    if (result) {
        result->id = newInode->id;
        result->mode = newInode->mode;
        result->size = newInode->size;
    }
    YUFS_LOG_INFO("created new one in %d with name %s", parent_id, name);
    return 0;
}

int YUFSCore_link(const char*, uint32_t target_id, uint32_t parent_id, const char* name) {

    if (target_id >= MAX_FILES || !inodeTable[target_id]) return -1;
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;

    struct YUFS_Inode* targetInode = inodeTable[target_id];
    struct YUFS_Inode* parentInode = inodeTable[parent_id];

    if (S_ISDIR(targetInode->mode)) return -1;
    if (!S_ISDIR(parentInode->mode)) return -1;

    struct YUFS_Dirent* newDirent = allocDirent(name, target_id);
    if (!newDirent) return -1;

    attach_dentry(parentInode->main_dentry, newDirent);

    targetInode->nlink++;
    YUFS_LOG_INFO("created new hardlink in %d with name %s on %d", parent_id, name, target_id);
    return 0;
}

int YUFSCore_unlink(const char*, uint32_t parent_id, const char* name) {
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;
    struct YUFS_Inode* parentInode = inodeTable[parent_id];

    struct YUFS_Dirent* targetDirent = find_child(parentInode->main_dentry, name);
    if (!targetDirent) return -1;

    struct YUFS_Inode* targetInode = inodeTable[targetDirent->inode_id];

    if (S_ISDIR(targetInode->mode)) return -1;

    if (targetDirent->prev_sibling) targetDirent->prev_sibling->next_sibling = targetDirent->next_sibling;
    else targetDirent->parent->first_child = targetDirent->next_sibling;

    if (targetDirent->next_sibling) targetDirent->next_sibling->prev_sibling = targetDirent->prev_sibling;

    YUFS_FREE(targetDirent);

    targetInode->nlink--;

    if (targetInode->nlink <= 0) {
        freeInode(targetInode);
    }
    YUFS_LOG_INFO("removed from %d with name %s", parent_id, name);
    return 0;
}

int YUFSCore_rmdir(const char*, uint32_t parent_id, const char* name) {
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;
    struct YUFS_Inode* parentInode = inodeTable[parent_id];
    struct YUFS_Dirent* targetDirent = find_child(parentInode->main_dentry, name);
    if (!targetDirent) return -1;
    struct YUFS_Inode* targetInode = inodeTable[targetDirent->inode_id];

    if (!S_ISDIR(targetInode->mode)) return -1;

    if (targetInode->main_dentry->first_child != NULL) return -1;

    if (targetDirent->prev_sibling) targetDirent->prev_sibling->next_sibling = targetDirent->next_sibling;
    else targetDirent->parent->first_child = targetDirent->next_sibling;
    if (targetDirent->next_sibling) targetDirent->next_sibling->prev_sibling = targetDirent->prev_sibling;

    YUFS_FREE(targetDirent);
    freeInode(targetInode);
    YUFS_LOG_INFO("removed dir in %d with name %s", parent_id, name);
    return 0;
}

int YUFSCore_read(const char*, uint32_t id, char *buf, size_t size, loff_t offset) {
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_Inode* node = inodeTable[id];

    if (S_ISDIR(node->mode)) return -1;
    if (!node->content || offset >= node->size) return 0;
    size_t available = node->size - offset;
    size_t to_read = (size < available) ? size : available;
    YUFS_MEMMOVE(buf, node->content + offset, to_read);
    YUFS_LOG_INFO("read from %d", id);
    return (int)to_read;
}

int YUFSCore_write(const char*, uint32_t id, const char *buf, size_t size, loff_t offset) {
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_Inode* node = inodeTable[id];
    if (S_ISDIR(node->mode)) return -1;

    size_t new_end = offset + size;
    if (new_end > node->size) {
        void* new_content = yu_realloc(node->content, node->size, new_end);
        if (!new_content) return -1;
        node->content = (char*)new_content;
        if (offset > node->size) YUFS_MEMSET(node->content + node->size, 0, offset - node->size);
        node->size = new_end;
    }
    YUFS_MEMMOVE(node->content + offset, buf, size);
    YUFS_LOG_INFO("write to %d", id);
    return (int)size;
}

int YUFSCore_iterate(const char*, uint32_t id, yufs_filldir_y callback, void* ctx, loff_t offset) {
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_Inode* inode = inodeTable[id];
    if (!S_ISDIR(inode->mode) || !inode->main_dentry) return -1;

    if (offset == 0) {
        if (!callback(ctx, ".", 1, inode->id, inode->mode)) return 0;
        offset++;
    }
    if (offset == 1) {

        struct YUFS_Dirent* parentDentry = inode->main_dentry->parent;
        struct YUFS_Inode* parentInode = inodeTable[parentDentry->inode_id];
        if (!callback(ctx, "..", 2, parentInode->id, parentInode->mode)) return 0;
        offset++;
    }

    struct YUFS_Dirent* child = inode->main_dentry->first_child;
    loff_t skip = (offset > 2) ? (offset - 2) : 0;

    while (child && skip > 0) { child = child->next_sibling; skip--; }

    while (child) {
        struct YUFS_Inode* childInode = inodeTable[child->inode_id];
        if (!callback(ctx, child->name, YUFS_STRLEN(child->name), childInode->id, childInode->mode)) return 0;
        child = child->next_sibling;
    }
    return 0;
}

int YUFSCore_getattr(const char*, uint32_t id, struct YUFS_stat* result) {
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_Inode* node = inodeTable[id];
    result->id = node->id;
    result->size = node->size;
    result->mode = node->mode;
    return 0;
}

#endif

#ifdef __WEB_VERSION__

#include "http.h"

#define TO_STR(buf, val, fmt) char buf[24]; snprintf(buf, sizeof(buf), fmt, val)
struct YUFS_packed_dirent {
    uint32_t id;
    char name[256];
    uint32_t type;
} __attribute__((packed));

int YUFSCore_init(void) { return 0; }
void YUFSCore_destroy(void) {}

int YUFSCore_lookup(const char* token, uint32_t parent_id, const char* name, struct YUFS_stat* result) {
    TO_STR(pid_str, parent_id, "%u");
    return (int)vtfs_http_call(token, "lookup", (char*)result, sizeof(struct YUFS_stat),
                                 2, "parent_id", pid_str, "name", name);
}

int YUFSCore_create(const char* token, uint32_t parent_id, const char* name, umode_t mode, struct YUFS_stat* result) {
    TO_STR(pid_str, parent_id, "%u");
    TO_STR(mode_str, mode, "%u");
    struct YUFS_stat temp_stat;
    int64_t ret = vtfs_http_call(token, "create", (char*)&temp_stat, sizeof(struct YUFS_stat),
                                 3, "parent_id", pid_str, "name", name, "mode", mode_str);
    if (ret == 0 && result) *result = temp_stat;
    return (int)ret;
}

int YUFSCore_link(const char* token, uint32_t target_id, uint32_t parent_id, const char* name) {
    TO_STR(tid_str, target_id, "%u");
    TO_STR(pid_str, parent_id, "%u");
    char dummy[64];
    return (int)vtfs_http_call(token, "link", dummy, sizeof(dummy), 3, "target_id", tid_str, "parent_id", pid_str, "name", name);
}

int YUFSCore_unlink(const char* token, uint32_t parent_id, const char* name) {
    TO_STR(pid_str, parent_id, "%u");
    char dummy[64];
    return (int)vtfs_http_call(token, "unlink", dummy, sizeof(dummy), 2, "parent_id", pid_str, "name", name);
}

int YUFSCore_rmdir(const char* token, uint32_t parent_id, const char* name) {
    TO_STR(pid_str, parent_id, "%u");
    char dummy[64];
    return (int)vtfs_http_call(token, "rmdir", dummy, sizeof(dummy), 2, "parent_id", pid_str, "name", name);
}

int YUFSCore_getattr(const char* token, uint32_t id, struct YUFS_stat* result) {
    TO_STR(id_str, id, "%u");
    return (int)vtfs_http_call(token, "getattr", (char*)result, sizeof(struct YUFS_stat), 1, "id", id_str);
}

int YUFSCore_read(const char* token, uint32_t id, char *buf, size_t size, loff_t offset) {
    TO_STR(id_str, id, "%u");
    TO_STR(sz_str, size, "%lu");
    TO_STR(off_str, offset, "%lld");

    char *kbuf = kmalloc(size, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    int64_t ret = vtfs_http_call(token, "read", kbuf, size,
                                 3, "id", id_str, "size", sz_str, "offset", off_str);
    if (ret > 0) memcpy(buf, kbuf, ret);
    kfree(kbuf);
    return (int)ret;
}

int YUFSCore_write(const char* token, uint32_t id, const char *buf, size_t size, loff_t offset) {
    TO_STR(id_str, id, "%u");
    TO_STR(off_str, offset, "%lld");

    char *encoded_buf = kmalloc(size * 3 + 1, GFP_KERNEL);
    if (!encoded_buf) return -ENOMEM;

    const char *hex = "0123456789ABCDEF";
    char *dst = encoded_buf;
    for (size_t i = 0; i < size; i++) {
        unsigned char c = (unsigned char)buf[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            *dst++ = c;
        } else {
            *dst++ = '%';
            *dst++ = hex[c >> 4];
            *dst++ = hex[c & 0x0F];
        }
    }
    *dst = '\0';

    char dummy[64];
    int64_t ret = vtfs_http_call(token, "write", dummy, sizeof(dummy),
                                 3, "id", id_str, "offset", off_str, "buf", encoded_buf);

    kfree(encoded_buf);
    return (int)ret;
}

int YUFSCore_iterate(const char* token, uint32_t id, yufs_filldir_y callback, void* ctx, loff_t offset) {
    TO_STR(id_str, id, "%u");
    struct YUFS_packed_dirent dentry;
    int current_offset = offset;

    while (1) {
        TO_STR(off_str, current_offset, "%d");
        int64_t ret = vtfs_http_call(token, "iterate", (char*)&dentry, sizeof(dentry),
                                     2, "id", id_str, "offset", off_str);
        if (ret != 0) break;
        size_t name_len = strnlen(dentry.name, sizeof(dentry.name));
        if (!callback(ctx, dentry.name, name_len, dentry.id, dentry.type)) return 0;
        current_offset++;
    }
    return 0;
}

#endif