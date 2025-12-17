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
    pthread_mutex_lock(&lock);

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
    pthread_mutex_unlock(&lock);
    return ret;
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

    pthread_mutex_lock(&lock);

    struct aesd_buffer_entry *entry = buffer->entry;
    entry[buffer->in_offs].buffptr = add_entry->buffptr;
    entry[buffer->in_offs].size = add_entry->size;
    buffer->in_offs += 1;


    if (buffer->full)
    {
        buffer->out_offs = buffer->in_offs;
    }
    else
    {
        buffer->in_offs %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        if (buffer->in_offs == buffer->out_offs)
            buffer->full = true;
    }
    pthread_mutex_unlock(&lock);
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    pthread_mutex_init(&lock, NULL);
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
