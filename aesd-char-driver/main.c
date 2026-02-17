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
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Brett Lange"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
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

    struct aesd_dev *device = filp->private_data;
    int err = mutex_lock_interruptible(&device->circular_buffer_mutex);
    if (err != 0)
    {
	    return err;
    }

    size_t offset = 0;
    struct aesd_buffer_entry *new_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&device->circular_buffer, *f_pos, &offset);
    if (new_entry != NULL)
    {
	    size_t num_chars = new_entry->size - offset;
	    
	    if (num_chars > count)
	    {
		    num_chars = count;
	    }
	    
	    err = copy_to_user(buf, new_entry->buffptr + offset, num_chars);
	    
	    if (err != 0)
	    {
		    mutex_unlock(&device->circular_buffer_mutex);
		    return err;
	    }
	    *f_pos += num_chars;
	    retval = num_chars;
    }

    mutex_unlock(&device->circular_buffer_mutex);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    struct aesd_dev *device = filp->private_data;

    int err = mutex_lock_interruptible(&device->buffer_entry_mutex);
    if (err != 0)
    {
	    return err;
    }
    
    char *temp_buff = kmalloc(device->current_entry.size + count, GFP_KERNEL);
    if (temp_buff == NULL)
    {
	    mutex_unlock(&device->buffer_entry_mutex);
	    return retval;
    }
    
    if (device->current_entry.buffptr != NULL)
    {
	    memcpy(temp_buff, device->current_entry.buffptr, device->current_entry.size);
	    kfree(device->current_entry.buffptr);
    }
    device->current_entry.buffptr = temp_buff;
    err = copy_from_user(device->current_entry.buffptr + device->current_entry.size, buf, count);
    if (err != 0)
    {
	    mutex_unlock(&device->buffer_entry_mutex);
	    return err;
    }
    device->current_entry.size += count;
    if (temp_buff[device->current_entry.size - 1] == '\n')
    {
        int err = mutex_lock_interruptible(&device->circular_buffer_mutex);
        if (err != 0)
        {
            mutex_unlock(&device->buffer_entry_mutex);
            return err;
        }
	    if (device->circular_buffer.full)
	    {
		    kfree(device->circular_buffer.entry[device->circular_buffer.in_offs].buffptr);
	    }
	    aesd_circular_buffer_add_entry(&device->circular_buffer, &device->current_entry);
	    memset(&device->current_entry, 0, sizeof(struct aesd_buffer_entry));
        mutex_unlock(&device->circular_buffer_mutex);
    }
    retval = count;
    mutex_unlock(&device->buffer_entry_mutex);

    return retval;
}
loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) 
{    
    struct aesd_dev *device = filp->private_data;

    ssize_t retval = mutex_lock_interruptible(&device->circular_buffer_mutex);
    if (retval != 0)
    {
        return -EINVAL;
    }
    retval = mutex_lock_interruptible(&device->buffer_entry_mutex);
    if (retval != 0)
    {
        mutex_unlock(&device->circular_buffer_mutex);
        return -EINVAL;
    }

    loff_t newpos = 0;
    switch(whence)
    {
        case SEEK_SET:
            newpos = offset;
            break;
        case SEEK_CUR:
            newpos = filp->f_pos + offset;
            break;
        case SEEK_END:
            ssize_t buffer_size = 0;
            size_t num_entries = 0;
            if (device->circular_buffer.full)
            {
                num_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            }
            else
            {
                num_entries = device->circular_buffer.in_offs - device->circular_buffer.out_offs;
                if (num_entries < 0)
                {
                    num_entries += AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                }
            }

            size_t current_index = device->circular_buffer.in_offs;
            for (int i = 0; i < num_entries; num_entries++)
            {
                buffer_size += device->circular_buffer.entry[current_index].size;
                ++current_index;
                if (current_index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
                {
                    current_index = 0;
                }
            }
            newpos = buffer_size + offset;
            break;
        default:
            mutex_unlock(&device->buffer_entry_mutex);
            mutex_unlock(&device->circular_buffer_mutex);
            PDEBUG("ERROR: Bad whence for llseek: %d", whence);
            return -EINVAL;
    }

    mutex_unlock(&device->buffer_entry_mutex);
    mutex_unlock(&device->circular_buffer_mutex);
    if (newpos < 0)
    {
        PDEBUG("ERROR: Negative new position: %d", newpos);
        return -EINVAL;
    }
    filp->f_pos = newpos;

    return newpos;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *device = filp->private_data;
    long newpos = 0;

    switch (cmd)
    {
        case (AESDCHAR_IOCSEEKTO):
            struct aesd_seekto seekto;
            size_t retval = copy_from_user(&seekto, (const void __user *)arg, sizeof(struct aesd_seekto));
            if (retval != 0)
            {
                return -EINVAL;
            }
            retval = mutex_lock_interruptible(&device->circular_buffer_mutex);
            if (retval != 0)
            {
                return -EINVAL;
            }
            retval = mutex_lock_interruptible(&device->buffer_entry_mutex);
            if (retval != 0)
            {
                mutex_unlock(&device->circular_buffer_mutex);
                return -EINVAL;
            }

            size_t num_entries = 0;
            if (device->circular_buffer.full)
            {
                num_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            }
            else
            {
                num_entries = device->circular_buffer.in_offs - device->circular_buffer.out_offs;
                if (num_entries < 0)
                {
                    num_entries += AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                }
            }

            if (seekto.write_cmd >= num_entries)
            {
                mutex_unlock(&device->buffer_entry_mutex);
                mutex_unlock(&device->circular_buffer_mutex);
                return -EINVAL;
            }

            size_t current_index = device->circular_buffer.in_offs;
            for (int i = 0; i < num_entries; num_entries++)
            {
                newpos += device->circular_buffer.entry[current_index].size;
                ++current_index;
                if (current_index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
                {
                    current_index = 0;
                }
            }

            mutex_unlock(&device->buffer_entry_mutex);
            mutex_unlock(&device->circular_buffer_mutex);
            if (device->circular_buffer.entry[current_index].size <= seekto.write_cmd_offset)
            {
                return -EINVAL;
            }

            newpos += seekto.write_cmd_offset;
            filp->f_pos = newpos;

            break;
        default:
            PDEBUG("ERROR: Bad cmd for llseek: %d", cmd);
            return -EINVAL;
    }

    return newpos;
}

struct file_operations aesd_fops = {
    .owner          = THIS_MODULE,
    .read           = aesd_read,
    .write          = aesd_write,
    .open           = aesd_open,
    .release        = aesd_release,
    .llseek         = aesd_llseek,
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

    mutex_init(&aesd_device.circular_buffer_mutex);
    mutex_init(&aesd_device.buffer_entry_mutex);
    aesd_circular_buffer_init(&aesd_device.circular_buffer);

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

    uint8_t i = 0;
    struct aesd_buffer_entry *entry = NULL;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, i)
    {
	    if (entry->buffptr != NULL)
	    {
		    kfree(entry->buffptr);
	    }
    }
    kfree(aesd_device.current_entry.buffptr);
    mutex_destroy(&aesd_device.circular_buffer_mutex);
    mutex_destroy(&aesd_device.buffer_entry_mutex);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
