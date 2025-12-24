# kernel module stuff
#include "yufs_core.h"

MODULE_LICENSE("GPL");

MODULE_AUTHOR("Yura");

MODULE_DESCRIPTION("YUFS - yuras simple Filesystem module");

#define YUFS_MAGIC 0x13131313


static const struct inode_operations yufs_dir_inode_ops;
static const struct file_operations yufs_dir_operations;
static const struct file_operations yufs_file_operations;


static struct inode *yufs_get_inode(struct super_block *sb, const struct YUFS_stat *stat) {
    struct inode *inode = new_inode(sb);
    if (!inode) return NULL;


    inode->i_ino = stat->id;
    inode->i_mode = stat->mode;
    inode->i_sb = sb;


    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);


    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &yufs_dir_inode_ops;
        inode->i_fop = &yufs_dir_operations;
        set_nlink(inode, 2);
    } else if (S_ISREG(inode->i_mode)) {
        inode->i_op = NULL;
        inode->i_fop = &yufs_file_operations;
        set_nlink(inode, 1);
        inode->i_size = stat->size;
    }

    return inode;
}


static ssize_t yufs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos) {
    struct inode *inode = file_inode(filp);
    void *kbuf;
    int bytes_read;

    if (len == 0) return 0;


    kbuf = kzalloc(len, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;


    bytes_read = YUFSCore_read(inode->i_ino, kbuf, len, *ppos);

    if (bytes_read < 0) {
        kfree(kbuf);
        return -EIO;
    }


    if (copy_to_user(buf, kbuf, bytes_read)) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);


    *ppos += bytes_read;
    return bytes_read;
}

static ssize_t yufs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos) {
    struct inode *inode = file_inode(filp);
    void *kbuf;
    int bytes_written;

    if (len == 0) return 0;

    kbuf = kzalloc(len, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    if (copy_from_user(kbuf, buf, len)) {
        kfree(kbuf);
        return -EFAULT;
    }


    bytes_written = YUFSCore_write(inode->i_ino, kbuf, len, *ppos);

    kfree(kbuf);

    if (bytes_written < 0) return -ENOSPC;

    *ppos += bytes_written;


    if (*ppos > inode->i_size) {
        inode->i_size = *ppos;
    }

    return bytes_written;
}


struct yufs_dir_ctx_adapter {
    struct dir_context *ctx;
};


static bool yufs_filldir_callback(void *priv, const char *name, int name_len, uint32_t id, umode_t type) {
    struct yufs_dir_ctx_adapter *adapter = (struct yufs_dir_ctx_adapter *) priv;

    return dir_emit(adapter->ctx, name, name_len, id, type);
}

static int yufs_iterate(struct file *filp, struct dir_context *ctx) {
    struct inode *inode = file_inode(filp);
    struct yufs_dir_ctx_adapter adapter = {.ctx = ctx};


    YUFSCore_iterate(inode->i_ino, yufs_filldir_callback, &adapter);

    return 0;
}


static struct dentry *yufs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    struct YUFS_stat stat;
    struct inode *inode = NULL;


    int ret = YUFSCore_lookup(parent_inode->i_ino, child_dentry->d_name.name, &stat);

    if (ret == 0) {
        inode = yufs_get_inode(parent_inode->i_sb, &stat);
    }


    d_add(child_dentry, inode);
    return NULL;
}

static int yufs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    struct YUFS_stat stat;

    int ret = YUFSCore_create(dir->i_ino, dentry->d_name.name, mode | S_IFREG, &stat);

    if (ret != 0) return -ENOSPC;

    struct inode *inode = yufs_get_inode(dir->i_sb, &stat);
    if (!inode) return -ENOMEM;

    d_add(dentry, inode);
    return 0;
}

static int yufs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode) {
    struct YUFS_stat stat;
    int ret = YUFSCore_create(dir->i_ino, dentry->d_name.name, mode | S_IFDIR, &stat);

    if (ret != 0) return -ENOSPC;

    struct inode *inode = yufs_get_inode(dir->i_sb, &stat);
    if (!inode) return -ENOMEM;


    inc_nlink(dir);
    d_add(dentry, inode);
    return 0;
}

static int yufs_unlink(struct inode *dir, struct dentry *dentry) {
    int ret = YUFSCore_unlink(dir->i_ino, dentry->d_name.name);
    return (ret == 0) ? 0 : -ENOENT;
}

static int yufs_rmdir(struct inode *dir, struct dentry *dentry) {
    int ret = YUFSCore_rmdir(dir->i_ino, dentry->d_name.name);
    if (ret == 0) {
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
};

static const struct inode_operations yufs_dir_inode_ops = {
    .lookup = yufs_lookup,
    .create = yufs_create,
    .mkdir = yufs_mkdir,
    .unlink = yufs_unlink,
    .rmdir = yufs_rmdir,
};


static void yufs_put_super(struct super_block *sb) {
    printk(KERN_INFO "YUFS: Superblock destroyed\n");
}

static const struct super_operations yufs_super_ops = {
    .put_super = yufs_put_super,
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode,
};

static int yufs_fill_super(struct super_block *sb, void *data, int silent) {
    struct inode *root_inode;
    struct YUFS_stat root_stat;


    if (YUFSCore_init() != 0) {
        return -ENOMEM;
    }


    sb->s_magic = YUFS_MAGIC;
    sb->s_op = &yufs_super_ops;


    if (YUFSCore_getattr(1000, &root_stat) != 0) {
        return -EINVAL;
    }


    root_inode = yufs_get_inode(sb, &root_stat);
    if (!root_inode) return -ENOMEM;


    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) return -ENOMEM;

    return 0;
}

static struct dentry *yufs_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name, void *data) {
    return mount_nodev(fs_type, flags, data, yufs_fill_super);
}

static void yufs_kill_sb(struct super_block *sb) {
    YUFSCore_destroy();
    kill_litter_super(sb);
}

static struct file_system_type yufs_fs_type = {
    .owner = THIS_MODULE,
    .name = "yufs",
    .mount = yufs_mount,
    .kill_sb = yufs_kill_sb,
    .fs_flags = FS_USERNS_MOUNT,
};


static int __init yufs_module_init(void) {
    int ret = register_filesystem(&yufs_fs_type);
    if (ret != 0) {
        printk(KERN_ERR "YUFS: Failed to register filesystem\n");
        return ret;
    }
    printk(KERN_INFO "YUFS: Module loaded successfully\n");
    return 0;
}

static void __exit yufs_module_exit(void) {
    unregister_filesystem(&yufs_fs_type);
    printk(KERN_INFO "YUFS: Module unloaded\n");
}

module_init(yufs_module_init);

module_exit(yufs_module_exit);
