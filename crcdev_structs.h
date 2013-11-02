#ifndef CRCDEV_STRUCTS_H
#define CRCDEV_STRUCTS_H

#include <linux/ioctl.h>
#include <linux/device.h>
#include <linux/completion.h>
#include <linux/semaphore.h>


#include "crcdev.h"

#define DRIVER_NAME     "crcdev"
#define BAR_SIZE        4096
#define MAX_DEVICES     256
#define MINOR_IN_USE    1
#define MINOR_FREE      0
#define CTX_IN_USE      1
#define CTX_FREE        0
#define BUFFER_SIZE     1024 * 16
#define WORKING         0
#define REMOVE_PENDING  1


struct crc_context {
    uint32_t poly;
    uint32_t sum;
};

struct crc_device {
    dev_t devno;
    struct cdev cdev;
    struct pci_dev *pcidev;
    /* Pointer to BAR0 */
    void __iomem *addr;
    /* Semaphore for device's contexts (value = number of contexts). */
    struct semaphore sem_device;
    /* Only one thread can use fetch data block at the same time. */
    struct semaphore sem_dev_busy;
    /* For device's registers and private data. */
    spinlock_t regs_lock;
    /* Indicates which contexts are free. */
    unsigned char ctx_status[CRCDEV_CTX_COUNT];
    /* Completion of computations (using fetch data block). One for each
     context. */
    struct completion fetch_data_event[CRCDEV_CTX_COUNT];
    /* Pointers to buffers. One for each context. */
    void *dma_buffer[CRCDEV_CTX_COUNT];
    dma_addr_t dma_handle[CRCDEV_CTX_COUNT];
    /* Number of active context. */
    int current_ctx;
    /* Number of currently opened files. */
    int open_files;
    /* When device is about to be removed, it must wait until all opened files
     became closed. */
    struct completion ready_to_remove_event;
};

struct file_priv_data {
    struct crc_context *ctx;
    struct crc_device *crcdev;
    struct semaphore sem_file;
};

#endif

