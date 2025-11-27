#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/blkdev.h>  // 确保包含

#define PROC_FILE_NAME "quec_ufs_size"
#define UFS_SIZE_PATH "/sys/block/sda/size"  // 修改为实际路径

static int read_file_to_buffer(const char *path, char *buffer, size_t size)
{
    struct file *file;
    loff_t pos = 0;
    int ret;

    file = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(file)) {
        printk(KERN_ERR "Failed to open file: %s\n", path);
        return PTR_ERR(file);
    }

    ret = kernel_read(file, buffer, size, &pos);
    filp_close(file, NULL);

    return ret;
}

static int quec_ufs_size_show(struct seq_file *m, void *v)
{
    char *kbuf;
    int length;
    u64 capacity = 0;

    kbuf = kmalloc(1024, GFP_KERNEL);
    if (!kbuf) {
        seq_puts(m, "Failed to allocate memory\n");
        return 0;
    }

    length = read_file_to_buffer(UFS_SIZE_PATH, kbuf, 1024);
    if (length > 0) {
        capacity = simple_strtoull(kbuf, NULL, 10);
        capacity *= 512;  // 扇区大小为 512 字节
    } else {
        capacity = 128ULL * 1024 * 1024 * 1024; // 默认 128 GB
        dev_warn(m->file->f_path.dentry->d_inode->i_sb->s_dev, "Using default UFS capacity: %llu GB\n",
                 capacity / (1024 * 1024 * 1024));
    }

    seq_printf(m, "%llu GB\n", capacity / (1024 * 1024 * 1024));
    kfree(kbuf);
    return 0;
}

static int quec_ufs_size_open(struct inode *inode, struct file *file)
{
    return single_open(file, quec_ufs_size_show, NULL);
}

static const struct proc_ops quec_ufs_size_fops = {
    .proc_open = quec_ufs_size_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init quec_ufs_size_init(void)
{
    proc_create(PROC_FILE_NAME, 0444, NULL, &quec_ufs_size_fops);
    return 0;
}

static void __exit quec_ufs_size_exit(void)
{
    remove_proc_entry(PROC_FILE_NAME, NULL);
}

module_init(quec_ufs_size_init);
module_exit(quec_ufs_size_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("james.liu");
MODULE_DESCRIPTION("A module to create /proc/quec_ufs_size");