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
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ashwin Ravindra"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp) {
    PDEBUG("open");
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp) {
    PDEBUG("release");
    /*Nothing to handle here. aesd_cleanup_module is responsible for cleanup*/
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    size_t* f_pos_offset = kmalloc(sizeof(size_t), GFP_KERNEL);
    struct aesd_buffer_entry* read_entry;

    struct aesd_dev *char_dev = filp->private_data;

    /*Lock the buffer and find the entry to read*/
    mutex_lock(&char_dev->read_write_lock); 

    /*If the entry is found, read the entry and copy to user*/
    if((read_entry = aesd_circular_buffer_find_entry_offset_for_fpos(char_dev->command_buffer, (size_t)*f_pos, f_pos_offset)) != NULL) {
        ssize_t bytes_read = read_entry->size - *f_pos_offset;
        if(bytes_read > count)
            bytes_read = count;
        copy_to_user(buf, read_entry->buffptr + *f_pos_offset, bytes_read);
        *f_pos += bytes_read;
        retval = bytes_read;
    }

    /*Unlock the buffer*/
    mutex_unlock(&char_dev->read_write_lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    ssize_t retval = -ENOMEM;

    struct aesd_dev *char_dev = filp->private_data;

    /*Temporary buffer to store the string from user*/
    char* temp_buffptr = kmalloc(count, GFP_KERNEL);
    copy_from_user(temp_buffptr, buf, count);

    /*If newline character not found, append to final_buffptr*/
    if(memchr(temp_buffptr, '\n', count) == NULL) {
        if(char_dev->final_buffptr == NULL) {
            char_dev->final_buffptr = kmalloc(count, GFP_KERNEL);
        }
        else {
            char_dev->final_buffptr = krealloc(char_dev->final_buffptr, char_dev->final_count + count, GFP_KERNEL);
        }
        memcpy(char_dev->final_buffptr + char_dev->final_count, temp_buffptr, count);
        PDEBUG("final_buffptr: %s", char_dev->final_buffptr);
        char_dev->final_count += count;
        kfree(temp_buffptr); //this is not needed anymore
        temp_buffptr = NULL; // Dont leave it dangling
    }

    else {
        /*If newline character found, append to final_buffptr and write to buffer*/
        if(char_dev->final_buffptr == NULL) {
            char_dev->final_buffptr = kmalloc(count, GFP_KERNEL);
        }
        else {
            char_dev->final_buffptr = krealloc(char_dev->final_buffptr, char_dev->final_count + count, GFP_KERNEL);
        }
        memcpy(char_dev->final_buffptr + char_dev->final_count, temp_buffptr, count);
        PDEBUG("final_buffptr: %s", char_dev->final_buffptr);
        char_dev->final_count += count;

        /*Write to the circular command buffer*/
        struct aesd_buffer_entry new_entry;
        new_entry.size = char_dev->final_count;
        new_entry.buffptr = kmalloc(char_dev->final_count, GFP_KERNEL);
        memcpy(new_entry.buffptr, char_dev->final_buffptr, char_dev->final_count);

        /*Lock before writing*/
        mutex_lock(&char_dev->read_write_lock);

        /*Write to the buffer*/
        const char* overwritten_buffptr = aesd_circular_buffer_add_entry(char_dev->command_buffer, &new_entry);

        /*Increment the buffer size*/
        char_dev->buffer_size += new_entry.size;

        /*Unlock after writing*/
        mutex_unlock(&char_dev->read_write_lock);

        /*If overwritten, free the overwritten entry*/
        if(overwritten_buffptr != NULL) {
            kfree(overwritten_buffptr);
        }
        char_dev->final_count = 0;
    }
    retval = count;
    *f_pos += count;
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) {
    struct aesd_dev *char_dev = filp->private_data;
    loff_t new_offset = 0;
    mutex_lock(&char_dev->read_write_lock);
    new_offset = fixed_size_llseek(filp, offset, whence, char_dev->buffer_size);
    PDEBUG("llseek to %lld",new_offset);
    mutex_unlock(&char_dev->read_write_lock);
    return new_offset;
}

static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset) {
    struct aesd_dev *char_dev = filp->private_data;
    long retval = 0;
    loff_t new_offset = 0;
    if(write_cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        retval = -EINVAL;
    }
    else if(char_dev->command_buffer->entry[write_cmd].buffptr == NULL) {
        retval = -EINVAL;
    }
    else if(write_cmd_offset > char_dev->command_buffer->entry[write_cmd].size) {
        retval = -EINVAL;
    }
    else {
        int i;
        for (i = 0; i < write_cmd; i++) {
            new_offset += char_dev->command_buffer->entry[i].size;
        }
        new_offset += write_cmd_offset;
        retval = new_offset;
        filp->f_pos = new_offset;
    }
    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    PDEBUG("ioctl");

    long retval = 0;
    switch(cmd)

    case AESDCHAR_IOCSEEKTO:
    {
        struct aesd_seekto seekto;
        if(copy_from_user(&seekto, (const void*)arg, sizeof(seekto))) {
            retval = EFAULT;
        }
        else {
            retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
        }
        break;
    }
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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
    PDEBUG("Inside aesd_init_module");
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    PDEBUG("Initializing AESD char driver");

    /*Allocate the circular buffer*/
    struct aesd_circular_buffer* buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    aesd_device.command_buffer = buffer;
    mutex_init(&aesd_device.read_write_lock); //initialize the mutex
    aesd_circular_buffer_init(aesd_device.command_buffer); //initialize the buffer
    aesd_device.command_buffer->in_offs = 0;
    aesd_device.command_buffer->out_offs = 0;
    aesd_device.command_buffer->full = false;
    aesd_device.final_buffptr = NULL; //initialize the final_buffptr with NULL
    aesd_device.final_count = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    PDEBUG("Cleaning up AESD char driver");
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /*Free each entry and its buffptr string in the circular buffer*/
    struct aesd_buffer_entry *entry;
    uint8_t index;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, aesd_device.command_buffer, index) {
        if(entry->buffptr != NULL)
            kfree(entry->buffptr);
    }

    /*Free the circular buffer and final_buffptr*/
    kfree(aesd_device.command_buffer);
    kfree(aesd_device.final_buffptr);

    mutex_destroy(&aesd_device.read_write_lock);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
