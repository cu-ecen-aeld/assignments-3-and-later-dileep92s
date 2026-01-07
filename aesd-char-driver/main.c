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
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "aesdchar.h"
#include "aesd-circular-buffer.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Dileep S"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
struct aesd_circular_buffer circular_buffer;

struct Node
{
    char *data;
    struct Node *next;
    size_t count;
};

static struct Node *head = NULL;
static size_t total_count = 0u;

struct Node *makeNode(const char __user *data, size_t count)
{
    struct Node *node = kmalloc(sizeof(struct Node), GFP_KERNEL);
    if (node)
    {
        node->data = kmalloc(count, GFP_KERNEL);
        if (node->data)
        {
            copy_from_user(node->data, data, count);
            node->count = count;
            node->next = NULL;
            return node;
        }
    }
    return NULL;
}

void addNode(struct Node **head, struct Node *next)
{
    if (next)
    {
        if (*head)
        {
            struct Node *ptr = *head;
            while (ptr->next)
                ptr = ptr->next;
            ptr->next = next;
        }
        else
        {
            *head = next;
        }
    }
}

void deleteList(struct Node *head)
{
    while (head)
    {
        struct Node *node = head;
        head = node->next;
        if (node->data != NULL)
            kfree(node->data);
        kfree(node);
    }
}

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
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

size_t get_available_data_size(void)
{
    size_t retval = 0;
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
        retval += circular_buffer.entry[i].size;
    return retval;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (*f_pos >= get_available_data_size())
        return 0;

    ssize_t retval = 0;
    char *tmp = kmalloc(count, GFP_KERNEL);
    retval = aesd_circular_buffer_find_entry_offset_for_fpos_and_copy(&circular_buffer, *f_pos, tmp, count);
    copy_to_user(buf, tmp, retval);
    kfree(tmp);

    PDEBUG("read complete %zu ", retval);
    *f_pos += retval;
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    bool completed = false;

    struct Node *node = makeNode(buf, count);
    if (node)
    {
        addNode(&head, node);
        retval = count;
        total_count += count;

        for (size_t i = 0; i < count; i++)
        {
            if ('\n' == node->data[i])
            {
                PDEBUG("W buf[%zu] \\n", i);
                completed = true;
            }
            else
            {
                PDEBUG("W buf[%zu] %c", i, node->data[i]);
            }
        }
    }

    if (completed)
    {
        char *fullbuff = kmalloc(total_count, GFP_KERNEL);
        size_t offset = 0u;
        if (fullbuff)
        {
            while (head)
            {
                struct Node *node = head;
                head = node->next;
                if (node->data != NULL)
                {
                    memcpy(fullbuff + offset, node->data, node->count);
                    offset += node->count;
                    kfree(node->data);
                }
                kfree(node);
            }
        }

        struct aesd_buffer_entry entry = {.buffptr = fullbuff, .size = total_count};
        aesd_circular_buffer_add_entry(&circular_buffer, &entry);
        total_count = 0u;
        PDEBUG("full %d outoff %d inoff %d", circular_buffer.full, circular_buffer.out_offs, circular_buffer.in_offs);
    }

    return retval;
}

loff_t aesd_seek(struct file *filp, loff_t off, int type)
{
    PDEBUG("seek type %zu with offset %lld", type, off);

    switch (type)
    {
    case SEEK_CUR:
        size_t avail = get_available_data_size();
        loff_t pos = avail + off;
        filp->f_pos = pos < avail ? pos : avail;
        break;
    case SEEK_SET:
        filp->f_pos = off;
        break;
    default:
        break;
    }
    return filp->f_pos;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_seek,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
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
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    aesd_circular_buffer_init(&circular_buffer);

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    deleteList(head);

    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
    {
        if (circular_buffer.entry[i].buffptr)
            kfree(circular_buffer.entry[i].buffptr);
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
