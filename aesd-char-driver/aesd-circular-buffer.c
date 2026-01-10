/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
static DEFINE_MUTEX(lock);
#else
#include <string.h>
#include <pthread.h>
#include <stdio.h>
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
                                                                          size_t char_offset, size_t *entry_offset_byte_rtn)
{
/**
 * TODO: implement per description
 */
#ifdef __KERNEL__
    mutex_lock(&lock);
#else
    pthread_mutex_lock(&lock);
#endif

    uint8_t idx = buffer->out_offs;
    uint8_t cnt = 0;
    size_t curr_offset = 0;
    struct aesd_buffer_entry *entry = buffer->entry;
    struct aesd_buffer_entry *ret = NULL;

    while ((cnt < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) && entry[idx].size)
    {
        if (char_offset < curr_offset + entry[idx].size)
        {
            *entry_offset_byte_rtn = char_offset - curr_offset;
            ret = &entry[idx];
            break;
        }

        curr_offset += entry[idx].size;
        idx += 1;
        idx %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        cnt++;
    }
#ifdef __KERNEL__
    mutex_unlock(&lock);
#else
    pthread_mutex_unlock(&lock);
#endif
    return ret;
}

size_t aesd_circular_buffer_find_entry_offset_for_fpos_and_copy(struct aesd_circular_buffer *buffer,
                                                                size_t char_offset, char *outbuffer, size_t count)
{

#ifdef __KERNEL__
    mutex_lock(&lock);
#else
    pthread_mutex_lock(&lock);
#endif

    uint8_t idx = buffer->out_offs;
    uint8_t cnt = 0;
    size_t curr_offset = 0;
    size_t entry_offset_byte = 0;
    struct aesd_buffer_entry *entry = buffer->entry;
    size_t byteswritten = 0;

    while ((cnt < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) && entry[idx].size)
    {
        if (char_offset < curr_offset + entry[idx].size)
        {
            entry_offset_byte = char_offset - curr_offset;
            break;
        }

        curr_offset += entry[idx].size;
        idx += 1;
        idx %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        cnt++;
    }

    if (cnt == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        return byteswritten;

    cnt = 0;
    while ((cnt < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) && entry[idx].size && byteswritten < count)
    {
        size_t bytestowrite = entry[idx].size - entry_offset_byte;
        bytestowrite = (count - byteswritten) < bytestowrite ? (count - byteswritten) : bytestowrite;
        memcpy(outbuffer + byteswritten, entry[idx].buffptr + entry_offset_byte, bytestowrite);
        byteswritten += bytestowrite;
        entry_offset_byte = 0;
        idx += 1;
        idx %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        cnt++;
    }

#ifdef __KERNEL__
    mutex_unlock(&lock);
#else
    pthread_mutex_unlock(&lock);
#endif
    return byteswritten;
}

long aesd_circular_buffer_find_offset(struct aesd_circular_buffer *buffer, uint32_t write_cmd, uint32_t write_cmd_offset)
{
#ifdef __KERNEL__
    mutex_lock(&lock);
#else
    pthread_mutex_lock(&lock);
#endif
    long offset = 0;
    uint8_t outoff = buffer->out_offs;

    printk(KERN_ERR "offset %d outoff %d", offset, outoff);

    while (write_cmd--)
    {
        if (buffer->entry[outoff].size == 0u)
            return -1;

        if (write_cmd)
        {
            printk(KERN_ERR "offset %d outoff %d size %d", offset, outoff, buffer->entry[outoff].size);
            offset += buffer->entry[outoff].size;
            outoff = (outoff + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        }
        else if (write_cmd_offset < buffer->entry[outoff].size)
        {
            printk(KERN_ERR "offset %d outoff %d size %d", offset, outoff, buffer->entry[outoff].size);
            offset += write_cmd_offset;
        }
        else
        {
            printk(KERN_ERR "offset %d outoff %d size %d", offset, outoff, buffer->entry[outoff].size);
            return -1;
        }
    }
#ifdef __KERNEL__
    mutex_unlock(&lock);
#else
    pthread_mutex_unlock(&lock);
#endif
    printk(KERN_ERR "offset %d", offset);
    return offset;
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
 * If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
 * new start location.
 * Any necessary locking must be handled by the caller
 * Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
 */
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
     * TODO: implement per description
     */

#ifdef __KERNEL__
    mutex_lock(&lock);
#else
    pthread_mutex_lock(&lock);
#endif

    struct aesd_buffer_entry *entry = buffer->entry;

#ifdef __KERNEL__
    if (entry[buffer->in_offs].buffptr)
    {
        kfree(entry[buffer->in_offs].buffptr);
        entry[buffer->in_offs].buffptr = NULL;
    }
#endif

    entry[buffer->in_offs].buffptr = add_entry->buffptr;
    entry[buffer->in_offs].size = add_entry->size;
    buffer->in_offs += 1;
    buffer->in_offs %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    if (buffer->full)
    {
        buffer->out_offs = buffer->in_offs;
    }
    else
    {
        if (buffer->in_offs == buffer->out_offs)
            buffer->full = true;
    }
#ifdef __KERNEL__
    mutex_unlock(&lock);
#else
    pthread_mutex_unlock(&lock);
#endif
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
#ifndef __KERNEL__
    pthread_mutex_init(&lock, NULL);
#endif
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}
