#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "yufs_core.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yura");

#define YUFS_MAGIC 0x13131313


struct yufs_sb_info {
    char token[64]; 
};

static const char* yufs_token(struct super_block *sb) {
    struct yufs_sb_info *sbi = sb->s_fs_info;
    return sbi ? sbi->token : "";
}

static const struct inode_operations yufs_dir_inode_ops;
static const struct file_operations yufs_dir_operations;
static const struct file_operations yufs_file_operations;

static int yufs_fsync(struct file *file, loff_t start, loff_t end, int datasync) { return 0; }

static struct inode *yufs_get_inode(struct super_block *sb, const struct YUFS_stat *stat, struct inode *dir) {
    struct inode *inode = new_inode(sb);
    if (!inode) return NULL;
    inode->i_ino = stat->id;
    inode->i_sb = sb;
    inode->i_mode = stat->mode;
    inode_init_owner(sb->s_user_ns, inode, dir, stat->mode);
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);

    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &yufs_dir_inode_ops;
        inode->i_fop = &yufs_dir_operations;
        set_nlink(inode, 2);
    } else if (S_ISREG(inode->i_mode)) {
        inode->i_fop = &yufs_file_operations;
        set_nlink(inode, 1);
        inode->i_size = stat->size;
    }
    return inode;
}

static ssize_t yufs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos) {
    struct inode *inode = file_inode(filp);
    
    const char* token = yufs_token(inode->i_sb);
    
    if (len == 0) return 0;
    void *kbuf = kzalloc(len, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    
    int bytes_read = YUFSCore_read(token, inode->i_ino, kbuf, len, *ppos);

    if (bytes_read < 0) { kfree(kbuf); return -EIO; }
    if (copy_to_user(buf, kbuf, bytes_read)) { kfree(kbuf); return -EFAULT; }
    kfree(kbuf);
    *ppos += bytes_read;
    return bytes_read;
}

static ssize_t yufs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos) {
    struct inode *inode = file_inode(filp);
    const char* token = yufs_token(inode->i_sb);

    if (len == 0) return 0;
    void *kbuf = kzalloc(len, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;
    if (copy_from_user(kbuf, buf, len)) { kfree(kbuf); return -EFAULT; }

    int bytes_written = YUFSCore_write(token, inode->i_ino, kbuf, len, *ppos);
    kfree(kbuf);
    if (bytes_written < 0) return -ENOSPC;

    *ppos += bytes_written;
    if (*ppos > inode->i_size) inode->i_size = *ppos;
    return bytes_written;
}

struct yufs_dir_ctx_adapter { struct dir_context *ctx; };

static bool yufs_filldir_callback(void *priv, const char *name, int name_len, uint32_t id, umode_t type) {
    struct yufs_dir_ctx_adapter *adapter = (struct yufs_dir_ctx_adapter *) priv;
    unsigned char dt_type = (S_ISDIR(type)) ? DT_DIR : DT_REG;
    bool res = dir_emit(adapter->ctx, name, name_len, id, dt_type);
    if (res) adapter->ctx->pos++;
    return res;
}

static int yufs_iterate(struct file *filp, struct dir_context *ctx) {
    struct inode *inode = file_inode(filp);
    const char* token = yufs_token(inode->i_sb);
    
    struct yufs_dir_ctx_adapter adapter = {.ctx = ctx};
    if (YUFSCore_iterate(token, inode->i_ino, yufs_filldir_callback, &adapter, ctx->pos) != 0) return -EINVAL;
    return 0;
}

static struct dentry *yufs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    struct YUFS_stat stat;
    struct inode *inode = NULL;
    const char* token = yufs_token(parent_inode->i_sb);

    int ret = YUFSCore_lookup(token, parent_inode->i_ino, child_dentry->d_name.name, &stat);
    if (ret == 0) {
        inode = yufs_get_inode(parent_inode->i_sb, &stat, parent_inode);
        if (!inode) return ERR_PTR(-ENOMEM);
    }
    d_add(child_dentry, inode);
    return NULL;
}

static int yufs_create(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    struct YUFS_stat stat;
    const char* token = yufs_token(dir->i_sb);

    if (YUFSCore_create(token, dir->i_ino, dentry->d_name.name, mode | S_IFREG, &stat) != 0) return -ENOSPC;
    struct inode *inode = yufs_get_inode(dir->i_sb, &stat, dir);
    if (!inode) return -ENOMEM;
    d_instantiate(dentry, inode);
    return 0;
}

static int yufs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry) {
    struct inode *inode = d_inode(old_dentry);
    const char* token = yufs_token(dir->i_sb);

    if (YUFSCore_link(token, inode->i_ino, dir->i_ino, dentry->d_name.name) != 0) return -ENOSPC;
    inc_nlink(inode);
    ihold(inode);
    d_instantiate(dentry, inode);
    return 0;
}

static int yufs_mkdir(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode) {
    struct YUFS_stat stat;
    const char* token = yufs_token(dir->i_sb);

    if (YUFSCore_create(token, dir->i_ino, dentry->d_name.name, mode | S_IFDIR, &stat) != 0) return -ENOSPC;
    struct inode *inode = yufs_get_inode(dir->i_sb, &stat, dir);
    if (!inode) return -ENOMEM;
    inc_nlink(dir);
    d_instantiate(dentry, inode);
    return 0;
}

static int yufs_unlink(struct inode *dir, struct dentry *dentry) {
    const char* token = yufs_token(dir->i_sb);
    return (YUFSCore_unlink(token, dir->i_ino, dentry->d_name.name) == 0) ? 0 : -ENOENT;
}

static int yufs_rmdir(struct inode *dir, struct dentry *dentry) {
    const char* token = yufs_token(dir->i_sb);
    if (YUFSCore_rmdir(token, dir->i_ino, dentry->d_name.name) == 0) {
        drop_nlink(dir);
        return 0;
    }
    return -ENOTEMPTY;
}

static const struct file_operations yufs_dir_operations = {
    .iterate_shared = yufs_iterate,
    .read = generic_read_dir,
    .llseek = generic_file_llseek,
};

static const struct file_operations yufs_file_operations = {
    .read = yufs_read,
    .write = yufs_write,
    .llseek = generic_file_llseek,
    .fsync = yufs_fsync,
};

static const struct inode_operations yufs_dir_inode_ops = {
    .lookup = yufs_lookup, .create = yufs_create, .mkdir = yufs_mkdir,
    .unlink = yufs_unlink, .rmdir = yufs_rmdir, .link = yufs_link,
};

static void yufs_put_super(struct super_block *sb) {
    
    kfree(sb->s_fs_info);
    sb->s_fs_info = NULL;
}

static const struct super_operations yufs_super_ops = {
    .put_super = yufs_put_super,
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode,
};

static int yufs_fill_super(struct super_block *sb, void *data, int silent) {
    struct inode *root_inode;
    struct YUFS_stat root_stat;
    struct yufs_sb_info *sbi;

    
    sbi = kzalloc(sizeof(struct yufs_sb_info), GFP_KERNEL);
    if (!sbi) return -ENOMEM;
    sb->s_fs_info = sbi;

    
    
    if (data) {
        strlcpy(sbi->token, (char*)data, sizeof(sbi->token));
    } else {
        strlcpy(sbi->token, "default", sizeof(sbi->token));
    }
    printk(KERN_INFO "YUFS: Mounting with token: %s\n", sbi->token);

    if (YUFSCore_init() != 0) return -ENOMEM;

    sb->s_magic = YUFS_MAGIC;
    sb->s_op = &yufs_super_ops;

    
    if (YUFSCore_getattr(sbi->token, 1000, &root_stat) != 0) return -EINVAL;

    root_inode = yufs_get_inode(sb, &root_stat, NULL);
    if (!root_inode) return -ENOMEM;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) return -ENOMEM;

    return 0;
}

static struct dentry *yufs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    if (!data || ((char*)data)[0] == 0) {
        data = (void*)dev_name;
    }
    return mount_nodev(fs_type, flags, data, yufs_fill_super);
}

static void yufs_kill_sb(struct super_block *sb) {
    kill_anon_super(sb);
    YUFSCore_destroy();
}

static struct file_system_type yufs_fs_type = {
    .owner = THIS_MODULE,
    .name = "yufs",
    .mount = yufs_mount,
    .kill_sb = yufs_kill_sb,
    .fs_flags = FS_USERNS_MOUNT,
};

static int __init yufs_module_init(void) {
    return register_filesystem(&yufs_fs_type);
}

static void __exit yufs_module_exit(void) {
    unregister_filesystem(&yufs_fs_type);
}

module_init(yufs_module_init);
module_exit(yufs_module_exit);