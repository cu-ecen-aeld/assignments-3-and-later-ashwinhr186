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
#include <linux/slab.h> // kmalloc
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ashwin Ravindra"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
char* final_buffptr = NULL;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    size_t* f_pos_offset = kmalloc(sizeof(size_t), GFP_KERNEL);
    struct aesd_buffer_entry* read_entry;
    mutex_lock(&aesd_device.write_lock); 
    if((read_entry = aesd_circular_buffer_find_entry_offset_for_fpos(aesd_device.command_buffer, (size_t)*f_pos, f_pos_offset)) != NULL) {
        PDEBUG("f_pos_offset: %zu", *f_pos_offset);
        PDEBUG("buffer->out_offs: %d", aesd_device.command_buffer->out_offs);
        PDEBUG("string read: %s", read_entry->buffptr + *f_pos_offset);
        ssize_t bytes_read = read_entry->size - *f_pos_offset;
        if(bytes_read > count)
            bytes_read = count;
        copy_to_user(buf, read_entry->buffptr + *f_pos_offset, bytes_read);
        *f_pos += bytes_read;
        PDEBUG("f_pos: %lld", *f_pos);
        retval = bytes_read;
    }
    mutex_unlock(&aesd_device.write_lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    ssize_t retval = -ENOMEM;
    static size_t final_count = 0;
    char* temp_buffptr = kmalloc(count, GFP_KERNEL);
    copy_from_user(temp_buffptr, buf, count);
    PDEBUG("temp_buffptr: %s", temp_buffptr);
    if(strchr(temp_buffptr, '\n') == NULL) {
        final_buffptr = krealloc(final_buffptr, final_count + count, GFP_KERNEL);
        memcpy(final_buffptr + final_count, temp_buffptr, count);
        PDEBUG("final_buffptr: %s", final_buffptr);
        final_count += count;
        retval = count;
        kfree(temp_buffptr);
        temp_buffptr = NULL;
        return retval;
    }
    /**
     * TODO: handle write
     */

    /*If newline character found, append to final_buffptr and write to buffer*/
    final_buffptr = krealloc(final_buffptr, final_count + count, GFP_KERNEL);
    memcpy(final_buffptr + final_count, temp_buffptr, count);
    PDEBUG("final_buffptr: %s", final_buffptr);
    final_count += count;

    struct aesd_buffer_entry *new_entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
    if (new_entry == NULL) {
        retval = -ENOMEM;
    }
    else {
        new_entry->size = final_count;
        new_entry->buffptr = kmalloc(final_count, GFP_KERNEL);
        memcpy(new_entry->buffptr, final_buffptr, final_count);
        PDEBUG("buffer->in_offs before write: %d", aesd_device.command_buffer->in_offs);
        PDEBUG("write string: %s", new_entry->buffptr);
        mutex_lock(&aesd_device.write_lock);
        const char* overwritten_buffptr = aesd_circular_buffer_add_entry(aesd_device.command_buffer, new_entry);
        mutex_unlock(&aesd_device.write_lock);
        PDEBUG("buffer->in_offs after write: %d", aesd_device.command_buffer->in_offs);
        if(overwritten_buffptr != NULL) {
            PDEBUG("overwritten_buffptr: %s", overwritten_buffptr);
            kfree(overwritten_buffptr);
        }
        retval = count;
        final_count = 0;
    }
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
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

int aesd_init_module(void)
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
    struct aesd_circular_buffer* buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    aesd_device.command_buffer = buffer;
    mutex_init(&aesd_device.write_lock);
    aesd_circular_buffer_init(aesd_device.command_buffer);
    aesd_device.command_buffer->in_offs = 0;
    aesd_device.command_buffer->out_offs = 0;
    aesd_device.command_buffer->full = false;
    final_buffptr = kmalloc(4, GFP_KERNEL);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    /*Use AESD_CIRCULAR_BUFFER_FOREACH perhaps to free each entry first*/
    kfree(aesd_device.command_buffer);
    mutex_destroy(&aesd_device.write_lock);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
