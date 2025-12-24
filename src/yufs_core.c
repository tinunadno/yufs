#include "yufs_core.h"

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
            return node;
        }
    }
    return NULL;
}

static struct YUFS_Dirent* allocDirent(const char* name, uint32_t inode_id) {
    struct YUFS_Dirent* d = (struct YUFS_Dirent*)YUFS_MALLOC(sizeof(struct YUFS_Dirent));
    if (!d) return NULL;
    YUFS_MEMSET(d, 0, sizeof(struct YUFS_Dirent));
    YUFS_STRCPY(d->name, name);
    d->inode_id = inode_id;
    return d;
}

static void freeInode(struct YUFS_Inode* node) {
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

int YUFSCore_lookup(uint32_t parent_id, const char* name, struct YUFS_stat* result) {
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

int YUFSCore_create(uint32_t parent_id, const char* name, umode_t mode, struct YUFS_stat* result) {
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
    return 0;
}

int YUFSCore_link(uint32_t target_id, uint32_t parent_id, const char* name) {

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

    return 0;
}

int YUFSCore_unlink(uint32_t parent_id, const char* name) {
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

    return 0;
}

int YUFSCore_rmdir(uint32_t parent_id, const char* name) {
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
    return 0;
}

int YUFSCore_read(uint32_t id, char *buf, size_t size, loff_t offset) {
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_Inode* node = inodeTable[id];

    if (S_ISDIR(node->mode)) return -1;
    if (!node->content || offset >= node->size) return 0;
    size_t available = node->size - offset;
    size_t to_read = (size < available) ? size : available;
    YUFS_MEMMOVE(buf, node->content + offset, to_read);
    return (int)to_read;
}

int YUFSCore_write(uint32_t id, const char *buf, size_t size, loff_t offset) {
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
    return (int)size;
}

int YUFSCore_iterate(uint32_t id, yufs_filldir_y callback, void* ctx, loff_t offset) {
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

int YUFSCore_getattr(uint32_t id, struct YUFS_stat* result) {
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_Inode* node = inodeTable[id];
    result->id = node->id;
    result->size = node->size;
    result->mode = node->mode;
    return 0;
}

#endif