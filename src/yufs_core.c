#include "yufs_core.h"

#ifdef __RAM_VERSION__

#define MAX_FILES 1024
#define ROOT_INO 1000

struct YUFS_direntNode
{
    uint32_t id;
    char name[MAX_NAME_SIZE];
    umode_t mode;

    struct YUFS_direntNode* parent;
    struct YUFS_direntNode* first_child;  
    struct YUFS_direntNode* next_sibling; 
    struct YUFS_direntNode* prev_sibling; 

    char* content;
    size_t size;
};

static struct YUFS_direntNode* inodeTable[MAX_FILES];

static struct YUFS_direntNode* allocDirEntNode(void)
{
    for (size_t i = 1; i < MAX_FILES; i++) 
    {
        if (inodeTable[i] == NULL)
        {
            struct YUFS_direntNode* node = (struct YUFS_direntNode*)YUFS_MALLOC(sizeof(struct YUFS_direntNode));
            if (!node) return NULL;
            YUFS_MEMSET(node, 0, sizeof(struct YUFS_direntNode));
            node->id = i;
            inodeTable[i] = node;
            return node;
        }
    }
    return NULL;
}

static void* yu_realloc(void* old, size_t oldSz, size_t newSz) 
{
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

int YUFSCore_init(void)
{
    YUFS_MEMSET(inodeTable, 0, sizeof(inodeTable));

    struct YUFS_direntNode* root = (struct YUFS_direntNode*)YUFS_MALLOC(sizeof(struct YUFS_direntNode));
    if (!root) return -1;
    YUFS_MEMSET(root, 0, sizeof(struct YUFS_direntNode));
    
    root->id = ROOT_INO;
    root->name[0] = '\0';
    root->mode = S_IFDIR | 0777;
    root->parent = root; 
    
    inodeTable[ROOT_INO] = root;
    return 0;
}

void YUFSCore_destroy(void) {
    for (size_t i = 0; i < MAX_FILES; i++)
    {
        if (inodeTable[i]) {
            if (inodeTable[i]->content) YUFS_FREE(inodeTable[i]->content);
            YUFS_FREE(inodeTable[i]);
            inodeTable[i] = NULL;
        }
    }
}

int YUFSCore_lookup(uint32_t parent_id, const char* name, struct YUFS_stat* result)
{
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;
    struct YUFS_direntNode* parent = inodeTable[parent_id];
    if (!S_ISDIR(parent->mode)) return -1;
    YUFS_LOG_INFO("Lookup for %d", parent_id);
    struct YUFS_direntNode* child = parent->first_child;
    int safety_counter = 0;
    while (child) {
        if (YUFS_STRCMP(child->name, name) == 0) {
            result->id = child->id;
            result->mode = child->mode;
            result->size = child->size;
            return 0;
        }
        child = child->next_sibling;
        if (++safety_counter > MAX_FILES) {
             YUFS_LOG_ERR("Lookup loop detected in dir %d", parent_id);
             break;
        }
    }
    
    return -1;
}

int YUFSCore_create(uint32_t parent_id, const char* name, umode_t mode, struct YUFS_stat* result)
{
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;
    struct YUFS_direntNode* parent = inodeTable[parent_id];

    if (!S_ISDIR(parent->mode)) return -1;
    if (YUFS_STRLEN(name) >= MAX_NAME_SIZE) return -1;

    struct YUFS_direntNode* newNode = allocDirEntNode();
    if (!newNode) return -1;

    YUFS_STRCPY(newNode->name, name);
    newNode->mode = mode;
    
    newNode->next_sibling = parent->first_child;
    newNode->prev_sibling = NULL; 
    
    if (parent->first_child) {
        parent->first_child->prev_sibling = newNode;
    }
    
    parent->first_child = newNode;

    if (result) {
        result->id = newNode->id;
        result->mode = newNode->mode;
        result->size = newNode->size;
    }
    return 0;
}

int YUFSCore_mkdir(uint32_t parent_id, const char* name, umode_t mode, struct YUFS_stat* result)
{
    return YUFSCore_create(parent_id, name, mode | S_IFDIR, result);
}

int YUFSCore_unlink(uint32_t parent_id, const char* name)
{
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;
    struct YUFS_direntNode* parent = inodeTable[parent_id];
    
    struct YUFS_direntNode* target = parent->first_child;
    while (target) {
        if (YUFS_STRCMP(target->name, name) == 0) break;
        target = target->next_sibling;
    }
    
    if (!target || S_ISDIR(target->mode)) return -1;

    if (target->prev_sibling) target->prev_sibling->next_sibling = target->next_sibling;
    else parent->first_child = target->next_sibling;

    if (target->next_sibling) target->next_sibling->prev_sibling = target->prev_sibling;

    if (target->content) YUFS_FREE(target->content);
    inodeTable[target->id] = NULL;
    YUFS_FREE(target);
    return 0;
}

int YUFSCore_rmdir(uint32_t parent_id, const char* name) 
{
    if (parent_id >= MAX_FILES || !inodeTable[parent_id]) return -1;
    struct YUFS_direntNode* parent = inodeTable[parent_id];
    
    struct YUFS_direntNode* target = parent->first_child;
    while (target) {
        if (YUFS_STRCMP(target->name, name) == 0) break;
        target = target->next_sibling;
    }
    
    if (!target || !S_ISDIR(target->mode)) return -1;
    if (target->first_child != NULL) return -1; 

    if (target->prev_sibling) target->prev_sibling->next_sibling = target->next_sibling;
    else parent->first_child = target->next_sibling;

    if (target->next_sibling) target->next_sibling->prev_sibling = target->prev_sibling;

    inodeTable[target->id] = NULL;
    YUFS_FREE(target);
    return 0;
}

int YUFSCore_getattr(uint32_t id, struct YUFS_stat* result) 
{
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_direntNode* node = inodeTable[id];
    result->id = id;
    result->size = node->size;
    result->mode = node->mode;
    return 0;
}

int YUFSCore_read(uint32_t id, char *buf, size_t size, loff_t offset) 
{
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_direntNode* node = inodeTable[id];
    
    if (S_ISDIR(node->mode)) return -1;
    if (!node->content || offset >= node->size) return 0;

    size_t available = node->size - offset;
    size_t to_read = (size < available) ? size : available;

    YUFS_MEMMOVE(buf, node->content + offset, to_read);
    return (int)to_read;
}

int YUFSCore_write(uint32_t id, const char *buf, size_t size, loff_t offset) 
{
    if (id >= MAX_FILES || !inodeTable[id]) return -1;
    struct YUFS_direntNode* node = inodeTable[id];
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

int YUFSCore_iterate(uint32_t id, yufs_filldir_y callback, void* ctx, loff_t offset) 
{

    if (id >= MAX_FILES) {
        YUFS_LOG_ERR("YUFS_ERR: iterate invalid id %d (max %d)\n", id, MAX_FILES);
        return -1;
    }

    if (!inodeTable[id]) {
        YUFS_LOG_ERR("YUFS_ERR: iterate node %d is NULL\n", id);
        return -1;
    }

    struct YUFS_direntNode* dir = inodeTable[id];

    if (!S_ISDIR(dir->mode)) {
        YUFS_LOG_ERR("YUFS_ERR: iterate node %d is NOT A DIR (mode=%o)\n", id, dir->mode);
        return -1;
    }

    YUFS_LOG_INFO("Iterate id=%d off=%lld", id, offset);
    
    if (offset == 0) {
        if (!callback(ctx, ".", 1, dir->id, dir->mode)) return 0;
        offset++;
    }

    
    if (offset == 1) {
        if (!callback(ctx, "..", 2, dir->parent->id, dir->parent->mode)) return 0;
        offset++;
    }

    struct YUFS_direntNode* child = dir->first_child;

    loff_t skip = (offset > 2) ? (offset - 2) : 0;
    int safety = 0;

    while (child && skip > 0) {
        child = child->next_sibling;
        skip--;
        
        if (++safety > MAX_FILES) {
            YUFS_LOG_ERR("Circular list in skip loop! id=%d", id);
            return 0;
        }
    }

    safety = 0;
    while (child) {
        
        if (!callback(ctx, child->name, YUFS_STRLEN(child->name), child->id, child->mode)) {
            return 0; 
        }
        child = child->next_sibling;
        
        if (++safety > MAX_FILES) {
            YUFS_LOG_ERR("Circular list in emit loop! id=%d", id);
            break; 
        }
    }
    
    return 0;
}

#endif