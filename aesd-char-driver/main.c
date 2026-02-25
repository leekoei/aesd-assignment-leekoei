/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/uaccess.h> // copy_from_user
#include "aesd-circular-buffer.h"
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("leekoei");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

static int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    
    filp->private_data = dev;

    return 0;
}

static int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t new_pos = 0;
    size_t total_size = 0;
    uint8_t index;
    struct aesd_buffer_entry *entryptr;

    PDEBUG("llseek offset %lld whence %d", offset, whence);

    mutex_lock(&dev->lock);

    /* Calculate total size of all entries in the circular buffer */
    AESD_CIRCULAR_BUFFER_FOREACH(entryptr, &dev->buffer, index) {
        /* Only count entries that are in the valid range */
        if (dev->buffer.full ||
            (dev->buffer.out_offs <= dev->buffer.in_offs ?
             (index >= dev->buffer.out_offs && index < dev->buffer.in_offs) :
             (index >= dev->buffer.out_offs || index < dev->buffer.in_offs))) {
            total_size += entryptr->size;
        }
    }

    /* Calculate new position based on whence */
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = filp->f_pos + offset;
            break;
        case SEEK_END:
            new_pos = total_size + offset;
            break;
        default:
            mutex_unlock(&dev->lock);
            return -EINVAL;
    }

    /* Validate the new position */
    if (new_pos < 0 || (size_t)new_pos > total_size) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = new_pos;
    mutex_unlock(&dev->lock);

    return new_pos;
}

static long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    struct aesd_buffer_entry *entryptr;
    int num_entries = 0;
    loff_t new_pos = 0;
    int i;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC || 
        _IOC_NR(cmd) > AESDCHAR_IOC_MAXNR || 
        cmd != AESDCHAR_IOCSEEKTO) {
        return -ENOTTY;
    }

    /* Copy the structure from user space */
    if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))) {
        return -EFAULT;
    }

    mutex_lock(&dev->lock);

    /* Count the number of valid entries in the circular buffer */
    if (dev->buffer.full) {
        num_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else {
        num_entries = (dev->buffer.in_offs - dev->buffer.out_offs + 
                      AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) % 
                      AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    /* Validate write_cmd is in range */
    if (seekto.write_cmd >= num_entries) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    /* Find the target entry and calculate position */
    /* Iterate through entries in chronological order (starting from out_offs) */
    for (i = 0; i < num_entries; i++) {
        uint8_t entry_index = (dev->buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entryptr = &dev->buffer.entry[entry_index];
        
        if (i == seekto.write_cmd) {
            /* Validate write_cmd_offset is within this entry's size */
            if (seekto.write_cmd_offset >= entryptr->size) {
                mutex_unlock(&dev->lock);
                return -EINVAL;
            }
            /* Calculate position: sum of all previous entries + offset */
            filp->f_pos = new_pos + seekto.write_cmd_offset;
            mutex_unlock(&dev->lock);
            return 0;
        }
        new_pos += entryptr->size;
    }

    /* Should not reach here if validation worked correctly */
    mutex_unlock(&dev->lock);
    return -EINVAL;
}

static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t bytes_to_copy, entry_offset;

    mutex_lock(&dev->lock);
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if (!entry || !entry->buffptr || entry_offset >= entry->size) {
        mutex_unlock(&dev->lock);
        return 0;
    }

    bytes_to_copy = entry->size - entry_offset;
    if (bytes_to_copy > count) {
        bytes_to_copy = count;
    }

    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) {
        mutex_unlock(&dev->lock);
        return -EFAULT;
    }

    *f_pos += bytes_to_copy;
    mutex_unlock(&dev->lock);

    return bytes_to_copy;
}

static ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    size_t size;
    size_t not_copied;
    char *newbuf;

    if (count == 0)
        return 0;

    mutex_lock(&dev->lock);

    size = dev->entry.size;
    if (size == 0)
        newbuf = kmalloc(count, GFP_KERNEL);
    else
        newbuf = krealloc((void *)dev->entry.buffptr, size + count, GFP_KERNEL);

    if (!newbuf) {
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }

    dev->entry.buffptr = newbuf;

    not_copied = copy_from_user((char *)dev->entry.buffptr + size, buf, count);
    retval = count - not_copied;
    dev->entry.size += retval;

    if ((retval > 0) && (((char *)dev->entry.buffptr)[dev->entry.size - 1] == '\n')) {
        const char *overwritten = aesd_circular_buffer_add_entry(&dev->buffer, &dev->entry);
        if (overwritten)
            kfree((void *)overwritten);
        dev->entry.buffptr = NULL;
        dev->entry.size = 0;
    }

    mutex_unlock(&dev->lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

static int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    /* Initialize the mutex */
    mutex_init(&aesd_device.lock);

    /* Initialize the circular buffer */
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

static void aesd_cleanup_module(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entryptr;

    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    /* Free the circular buffer entries */
    AESD_CIRCULAR_BUFFER_FOREACH(entryptr,&aesd_device.buffer,index) {
        kfree(entryptr->buffptr);
    }

    kfree(aesd_device.entry.buffptr);

    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
