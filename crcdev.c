#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/semaphore.h>
#include <asm/spinlock.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "crcdev.h"
#include "crcdev_ioctl.h"
#include "crcdev_structs.h"


/* Spinlock for global variables. */
spinlock_t driver_lock = SPIN_LOCK_UNLOCKED;
/* Class for crc devices. */
struct class *crcdev_class;
/* Major number of crc devices. */
int crcdev_major = 0;
/* Indicates whether minor number is free. */
unsigned char minor_status[MAX_DEVICES];
/* There are some problems with reusing minors, so each minor is used at most
 once. */
int first_unused_minor = 0;
/* Contains structs for each device. */
struct crc_device *crc_devices[MAX_DEVICES];
/* Inidcates if i-th device is working or is about to be removed. */
unsigned char device_status[MAX_DEVICES];
/* Indicates if driver is working or is about to be removed. */
unsigned char driver_status;

static int crcdev_init_module(void);
static void crcdev_exit_module(void);

static int crcdev_open(struct inode *inode, struct file *filp);
static int crcdev_release(struct inode *inode, struct file *filp);
static ssize_t crcdev_write(struct file *filp, const char __user *buff,
                            size_t count, loff_t *offp);
static int crcdev_ioctl(struct inode *inode, struct file *filp,
                        unsigned int cmd, unsigned long arg);

static int crcdev_probe(struct pci_dev *pcidev, const struct pci_device_id *id);
static void crcdev_remove(struct pci_dev *pcidev);

/* */
static struct file_operations crcdev_file_ops = {
    .owner          = THIS_MODULE,
    .open           = crcdev_open,
    .release        = crcdev_release,
    .write          = crcdev_write,
    .ioctl          = crcdev_ioctl,
};

/* */
static const struct pci_device_id crcdev_id = { 
    PCI_DEVICE(CRCDEV_VENDOR_ID, CRCDEV_DEVICE_ID)
};

/* */
static struct pci_driver crcdev_driver = {
    .name       = "crcdev_driver",
    .id_table   = &crcdev_id,
    .probe      = crcdev_probe,
    .remove     = crcdev_remove,
};

/* Gets first free minor. */
static int get_free_minor(void)
{
    return first_unused_minor++;
}

/* */
static int get_free_context(struct crc_device *crcdev)
{
    int i = 0;
    for (i = 0; i < CRCDEV_CTX_COUNT; ++i)
        if (crcdev->ctx_status[i] == CTX_FREE)
            break;
    return i;
}

/* Interrupt handler. */
static irqreturn_t crcdev_irq_handler(int irq, void *data)
{
    struct crc_device *crcdev = (struct crc_device *) data;
    u32 ctl;
    unsigned long flags;
    spin_lock_irqsave(&crcdev->regs_lock, flags);
    ctl = ioread32(crcdev->addr + CRCDEV_INTR);

    if (ctl & CRCDEV_INTR_FETCH_DATA)
    {
        /* Mark that interrupt was consumed. */
        iowrite32(1, crcdev->addr + CRCDEV_FETCH_DATA_INTR_ACK);
        if (crcdev->current_ctx >= 0)
            complete(&crcdev->fetch_data_event[crcdev->current_ctx]);
    }
    else if (ctl & CRCDEV_INTR_FETCH_CMD_IDLE)
    {
        // not supported.
    }
    else if (ctl & CRCDEV_INTR_FETCH_CMD_NONFULL)
    {
        // not supported.
    }
    else
    {
        spin_unlock_irqrestore(&crcdev->regs_lock, flags);
        return IRQ_NONE;
    }

    spin_unlock_irqrestore(&crcdev->regs_lock, flags);
    return IRQ_HANDLED;
}

/* Function called first. */
static int __init crcdev_init_module(void)
{
    int i;
    int result = 0;

    /* Initialize structures. */
    for (i = 0; i < MAX_DEVICES; ++i)
    {
        crc_devices[i] = NULL;
        minor_status[i] = MINOR_FREE;
    }
    
    driver_status = WORKING;

    /* Create class. */
    crcdev_class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(crcdev_class))
    {
        result = PTR_ERR(crcdev_class);
        printk(KERN_ERR "class_create failed.\n");
        goto fail_class_create;
    }

    /* Register driver. */
    result = pci_register_driver(&crcdev_driver);
    if (result)
    {
        printk(KERN_ERR "pci_register_driver_failed.\n");
        goto fail_register_driver;
    }

    printk(KERN_INFO "CRC driver registered.\n");
    return 0;

fail_register_driver:
    class_destroy(crcdev_class);
fail_class_create:
    return result;
}

/* */
static int crcdev_open(struct inode *inode, struct file *filp)
{
    int minor;
    struct crc_device *crcdev;
    unsigned long flags;
    struct crc_context *ctx;
    struct file_priv_data *priv_data;

    /* Get device associated with current file. */
    spin_lock_irqsave(&driver_lock, flags);
    minor = iminor(inode);
    /* When device is about to be removed we do not handle new requests. */
    if (device_status[minor] == REMOVE_PENDING)
    {   
        spin_unlock_irqrestore(&driver_lock, flags);
        return -ENXIO;
    }
    crcdev = crc_devices[minor];
    spin_unlock_irqrestore(&driver_lock, flags);

    /* Create context. */
    ctx = (struct crc_context *) 
        kzalloc(sizeof(struct crc_context), GFP_KERNEL);
    if (ctx == NULL)
    {
        goto fail_ctx_alloc;
    }
    /* Create file's private data. */
    priv_data = (struct file_priv_data *)
        kzalloc(sizeof(struct file_priv_data), GFP_KERNEL);
    if (priv_data == NULL)
    {
        goto fail_priv_data_alloc;
    }
    /* Initialize file's private data. */
    priv_data->ctx = ctx;
    priv_data->crcdev = crcdev;
    filp->private_data = priv_data;
    sema_init(&priv_data->sem_file, 1);
    spin_lock_irqsave(&crcdev->regs_lock, flags);
    crcdev->open_files++;
    spin_unlock_irqrestore(&crcdev->regs_lock, flags);
    return 0;

fail_priv_data_alloc:
    kfree(ctx);
fail_ctx_alloc:
    return 1;
}

/* */
static int crcdev_release(struct inode *inode, struct file *filp)
{
    struct file_priv_data *priv_data;
    struct crc_device *crcdev;
    struct crc_context *ctx;
    unsigned long flags, flags2;
    int minor;

    minor = iminor(inode);
    priv_data = (struct file_priv_data *) filp->private_data;
    crcdev = (struct crc_device *) priv_data->crcdev;
    ctx = (struct crc_context *) priv_data->ctx;
    kfree(ctx);
    kfree(priv_data);

    spin_lock_irqsave(&crcdev->regs_lock, flags);
    crcdev->open_files--;
    if (crcdev->open_files == 0)
    {
        spin_lock_irqsave(&driver_lock, flags2);
        if (device_status[minor] == REMOVE_PENDING)
        {
            complete(&crcdev->ready_to_remove_event);
        }
        spin_unlock_irqrestore(&driver_lock, flags2);
    }
    spin_unlock_irqrestore(&crcdev->regs_lock, flags);
    return 0;
}

/* */
static ssize_t crcdev_write(struct file *filp, const char __user *buff,
                            size_t count, loff_t *offp)
{
    struct file_priv_data *priv_data;
    struct crc_device *crcdev;
    struct crc_context *ctx;
    int ctx_no = -1;
    unsigned long flags;
    size_t sent = 0, to_send;
    int result;

    priv_data = (struct file_priv_data *) filp->private_data;
    crcdev = (struct crc_device *) priv_data->crcdev;
    ctx = (struct crc_context *) priv_data->ctx;
    
    /* Only one thread can "work" with file at the same time. */
    if (down_interruptible(&priv_data->sem_file))
    {
        return sent;
    }
    /* Try to get a free device's context. */
    if (down_interruptible(&crcdev->sem_device))
    {
        result = sent;
        goto intr_sem_dev;
    }
    spin_lock_irqsave(&crcdev->regs_lock, flags);
    /* Get the free context number. */
    ctx_no = get_free_context(crcdev);
    crcdev->ctx_status[ctx_no] = CTX_IN_USE;
    /* Set initial values. */
    iowrite32(ctx->sum, crcdev->addr + CRCDEV_CRC_SUM(ctx_no));
    iowrite32(ctx->poly, crcdev->addr + CRCDEV_CRC_POLY(ctx_no));
    spin_unlock_irqrestore(&crcdev->regs_lock, flags);
   
    sent = 0;
    while (sent < count)
    {
        /* Copy user data to DMA buffer. */
        to_send = (BUFFER_SIZE < count - sent) ? BUFFER_SIZE : count - sent;
        if(copy_from_user(crcdev->dma_buffer[ctx_no], buff + sent, to_send))
        { 
            printk(KERN_ERR "copy_to_user failed!\n");
            result = sent;
            goto intr_sems_dev_file;
        }
        sent += to_send;

        /* Wait until device is not busy. */
        if (down_interruptible(&crcdev->sem_dev_busy))
        {
            result = sent;
            goto intr_sems_dev_file;
        };
        spin_lock_irqsave(&crcdev->regs_lock, flags);
        /* Set current context number. */
        crcdev->current_ctx = ctx_no;

        /* Set registers of fetch data block.  The order is important! */
        iowrite32(ctx_no, crcdev->addr + CRCDEV_FETCH_DATA_CTX);
        iowrite32(crcdev->dma_handle[ctx_no], crcdev->addr + CRCDEV_FETCH_DATA_ADDR);
        iowrite32(to_send, crcdev->addr + CRCDEV_FETCH_DATA_COUNT);
                
        /* Wait for computation completion. */
        init_completion(&crcdev->fetch_data_event[ctx_no]);
        spin_unlock_irqrestore(&crcdev->regs_lock, flags);
        wait_for_completion(&crcdev->fetch_data_event[ctx_no]); // always wait!
        up(&crcdev->sem_dev_busy);
    }

    /* Copy final values. Free context. */
    spin_lock_irqsave(&crcdev->regs_lock, flags);
    ctx->sum = ioread32(crcdev->addr + CRCDEV_CRC_SUM(ctx_no));
    crcdev->ctx_status[ctx_no] = CTX_FREE;
    spin_unlock_irqrestore(&crcdev->regs_lock, flags);
   
    /* Enable other client to use this context. */
    up(&crcdev->sem_device);
    /* */
    up(&priv_data->sem_file);
    return sent;

intr_sems_dev_file:
    up(&crcdev->sem_device);
intr_sem_dev:
    if (ctx_no >= 0)
    {
        spin_lock_irqsave(&crcdev->regs_lock, flags);
        crcdev->ctx_status[ctx_no] = CTX_FREE;
        spin_unlock_irqrestore(&crcdev->regs_lock, flags);
    }
    up(&priv_data->sem_file);
    return result;
}

/* */
static int crcdev_ioctl(struct inode *inode, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
    int result = 0;
    struct file_priv_data *priv_data;
    struct crc_device *crcdev;
    struct crc_context *ctx;

    priv_data = (struct file_priv_data *) filp->private_data;
    crcdev = (struct crc_device *) priv_data->crcdev;
    ctx = (struct crc_context *) priv_data->ctx;

    if (down_interruptible(&priv_data->sem_file))
    {
        return -EINTR;
    }

    switch (cmd) {
    case CRCDEV_IOCTL_SET_PARAMS: {
        struct crcdev_ioctl_set_params params;
        struct __user crcdev_ioctl_set_params *argp;
        argp = (struct __user crcdev_ioctl_set_params *) arg;
        if (copy_from_user(&params, argp, sizeof(params)))
        {
            result = -EFAULT;
            goto fail;
        }
        ctx->poly = argp->poly;
        ctx->sum = argp->sum;
        break;
    }    
    case CRCDEV_IOCTL_GET_RESULT: {
        struct crcdev_ioctl_get_result res;
        struct __user crcdev_ioctl_get_result *argp;
        argp = (struct __user crcdev_ioctl_get_result *) arg;
        res.sum = ctx->sum;
        if (copy_to_user(argp, &res, sizeof(struct crcdev_ioctl_get_result)))
        {
            result = -EFAULT;
            goto fail;
        }
        break;
    }
    default:
        result = -ENOTTY;
        goto fail;
    }
    up(&priv_data->sem_file);
    return 0;

fail:
    up(&priv_data->sem_file);
    return result;
}

/* Adds new device when PCI bus signals. */
static int crcdev_probe(struct pci_dev *pcidev, const struct pci_device_id *id)
{
    int result = 0;
    struct crc_device *crcdev = NULL;
    dev_t dev = 0;
    int crcdev_minor = 0;
    unsigned long flags;
    int i;

    /* Check if there is free minor for new device. */
    spin_lock_irqsave(&driver_lock, flags);
    if (driver_status == REMOVE_PENDING)
    {
        spin_unlock_irqrestore(&driver_lock, flags); 
        dev_err(&pcidev->dev, "Driver is about to be removed.\n");
        return -ENXIO;
    }
    crcdev_minor = get_free_minor();
    if (crcdev_minor >= MAX_DEVICES)
    {
        spin_unlock_irqrestore(&driver_lock, flags);
        dev_err(&pcidev->dev, "Too many devices found.\n");
        goto fail_max_devices;
    }
    minor_status[crcdev_minor] = MINOR_IN_USE;
    spin_unlock_irqrestore(&driver_lock, flags);

    /* Get major number if the first device is being added. */
    if (crcdev_major)
    {
        dev = MKDEV(crcdev_major, crcdev_minor);
        result = register_chrdev_region(dev, 1, DRIVER_NAME);
        if (result < 0)
        {
            dev_err(&pcidev->dev, "register_chrdev_region failed.\n");
            goto fail_register_alloc_chrdev_region;
        }
    }
    else
    {
        result = alloc_chrdev_region(&dev, crcdev_minor, 1, DRIVER_NAME);
        if (result < 0)
        {
            dev_err(&pcidev->dev, "alloc_chrdev_region failed.\n");
            goto fail_register_alloc_chrdev_region;
        }
        crcdev_major = MAJOR(dev);
    }

    /* */
    result = pci_enable_device(pcidev);
    if (result)
    {
        dev_err(&pcidev->dev, "pci_enable_device failed.\n");
        goto fail_enable_device;
    }

    /* */
    result = pci_request_regions(pcidev, DRIVER_NAME);
    if (result)
    {
        dev_err(&pcidev->dev, "pci_request_regions failed.\n");
        goto fail_request_regions;
    }

    /* Allocate structure for new device. */
    crcdev = (struct crc_device *) kzalloc(sizeof(struct crc_device), GFP_KERNEL);
    if (crcdev == NULL)
    {
        dev_err(&pcidev->dev, "failed to allocate device.\n");
        result = -ENOMEM;
        goto fail_kmalloc;
    }

    crcdev->addr = pci_iomap(pcidev, 0, BAR_SIZE);
    if (crcdev->addr == NULL)
    {
        dev_err(&pcidev->dev, "pci_iomap failed.\n");
        result = -ENOMEM;
        goto fail_iomap;
    }

    /* Initialize other fields. */
    crcdev->devno = MKDEV(crcdev_major, crcdev_minor);
    crcdev->pcidev = pcidev;
    crcdev->current_ctx = -1;
    crcdev->open_files = 0;
    init_completion(&crcdev->ready_to_remove_event);

    /* Initialize contexts. */
    for (i = 0; i < CRCDEV_CTX_COUNT; ++i)
    {
        crcdev->ctx_status[i] = CTX_FREE;
        crcdev->dma_buffer[i] = NULL;
    }

    /* Initialize semaphores and spinlocks. */
    sema_init(&crcdev->sem_device, CRCDEV_CTX_COUNT);
    sema_init(&crcdev->sem_dev_busy, 1);
    spin_lock_init(&crcdev->regs_lock);
    
    /* Initialize cdev struct. */
    cdev_init(&crcdev->cdev, &crcdev_file_ops);
    crcdev->cdev.owner = THIS_MODULE;
   
    /* Set registers default values. */
    iowrite32(0, crcdev->addr + CRCDEV_FETCH_DATA_ADDR);
    iowrite32(0, crcdev->addr + CRCDEV_FETCH_DATA_COUNT);
    iowrite32(0, crcdev->addr + CRCDEV_FETCH_DATA_CTX);
    iowrite32(0, crcdev->addr + CRCDEV_FETCH_DATA_INTR_ACK);
    /* Enable fetch data block. */
    iowrite32(CRCDEV_ENABLE_FETCH_DATA, crcdev->addr + CRCDEV_ENABLE);
    /* Enable interrupts (fetch_data). */
    iowrite32(CRCDEV_INTR_FETCH_DATA, crcdev->addr + CRCDEV_INTR_ENABLE);

    /* Register interrupt handler. */
    result = request_irq(pcidev->irq, crcdev_irq_handler, IRQF_SHARED,
            DRIVER_NAME, crcdev);
    if (result)
    {
        dev_err(&pcidev->dev, "request_irq failed.\n");
        goto fail_request_irq;
    }

    /* Add cdev. */
    result = cdev_add(&crcdev->cdev, crcdev->devno, 1);
    if (result)
    {
        dev_err(&pcidev->dev, "cdev_add failed.\n");
        goto fail_cdev_add;
    }

    /* Enable DMA. */
    pci_set_master(pcidev);
    result = pci_set_dma_mask(pcidev, DMA_BIT_MASK(32));
    if (result)
    {
        dev_err(&pcidev->dev, "set_dma_mask failed.\n");
        goto fail_set_dma_mask;
    }
    result = pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(32));
    if (result)
    {
        dev_err(&pcidev->dev, "set_consistent_dma_mask failed.\n");
        goto fail_set_consistent_dma_mask;
    }

    /* Create DMA buffer. */
    for (i = 0; i < CRCDEV_CTX_COUNT; ++i)
    {
        crcdev->dma_buffer[i] = dma_alloc_coherent(&pcidev->dev, BUFFER_SIZE,
                &crcdev->dma_handle[i], GFP_KERNEL);
        if (crcdev->dma_buffer[i] == NULL)
        {
            dev_err(&pcidev->dev, "dma_alloc_coherent failed.\n");
            goto fail_dma_alloc_coherent;
        }
    }

    /* Create sysfs entry. */
    if (IS_ERR(device_create(crcdev_class, &pcidev->dev, crcdev->devno, NULL,
                    "crc%d", crcdev_minor)))
    {
        dev_err(&pcidev->dev, "Can't create sysfs entry.\n");
        goto fail_device_create;
    }

    /* Set device's private data. */
    crc_devices[crcdev_minor] = crcdev;
    pci_set_drvdata(pcidev, crcdev);

    spin_lock_irqsave(&driver_lock, flags);
    device_status[crcdev_minor] = WORKING;
    spin_unlock_irqrestore(&driver_lock, flags);

    printk(KERN_NOTICE "Character device successfully added (%d,%d).\n",
            MAJOR(dev), MINOR(dev));
    return 0;

fail_device_create:
    for (i = 0; i < CRCDEV_CTX_COUNT; ++i)
        if (crcdev->dma_buffer != NULL)
            dma_free_coherent(&pcidev->dev, BUFFER_SIZE, crcdev->dma_buffer[i],
                    crcdev->dma_handle[i]); 
fail_dma_alloc_coherent:
fail_set_consistent_dma_mask:
fail_set_dma_mask:
    cdev_del(&crcdev->cdev);
fail_cdev_add:
    free_irq(pcidev->irq, crcdev);
fail_request_irq:
    pci_iounmap(pcidev, crcdev->addr);
fail_iomap:
    kfree(crcdev);
fail_kmalloc:
    pci_release_regions(pcidev);
fail_request_regions:
    pci_disable_device(pcidev);
fail_enable_device:
    unregister_chrdev_region(MKDEV(crcdev_major, crcdev_minor), 1);    
fail_register_alloc_chrdev_region:
    spin_lock_irqsave(&driver_lock, flags);
    minor_status[crcdev_minor] = MINOR_FREE;
    crc_devices[crcdev_minor] = NULL;
    spin_unlock_irqrestore(&driver_lock, flags);
fail_max_devices:
    return result;
}

/* */
static void crcdev_remove(struct pci_dev *pcidev)
{
    struct crc_device *crcdev = (struct crc_device *) pci_get_drvdata(pcidev);
    int idx = MINOR(crcdev->devno);
    unsigned long flags;
    int i;

    /* Set flag. Refuse to call open, but allow current clients to finish
     their job. */
    spin_lock_irqsave(&driver_lock, flags);
    device_status[idx] = REMOVE_PENDING;
    spin_unlock_irqrestore(&driver_lock, flags);

    /* If there is at least one open file, we have to wait until all open files 
       are closed. */
    spin_lock_irqsave(&crcdev->regs_lock, flags);
    if (crcdev->open_files > 0)
    {
        spin_unlock_irqrestore(&crcdev->regs_lock, flags);
        wait_for_completion(&crcdev->ready_to_remove_event);
    }
    else
    {
        spin_unlock_irqrestore(&crcdev->regs_lock, flags);
    }

    /* Leave ENABLE and INTR_ENABLE with default value. */
    iowrite32(0, crcdev->addr + CRCDEV_ENABLE);
    iowrite32(0, crcdev->addr + CRCDEV_INTR_ENABLE);

    /* Free resources. */
    device_destroy(crcdev_class, crcdev->devno);
    for (i = 0; i < CRCDEV_CTX_COUNT; ++i)
        if (crcdev->dma_buffer != NULL)
            dma_free_coherent(&pcidev->dev, BUFFER_SIZE, crcdev->dma_buffer[i],
                crcdev->dma_handle[i]);
    cdev_del(&crcdev->cdev);
    free_irq(pcidev->irq, crcdev);
    pci_iounmap(pcidev, crcdev->addr);
    kfree(crcdev);
    pci_release_regions(crcdev->pcidev);
    pci_disable_device(crcdev->pcidev);
    unregister_chrdev_region(crcdev->devno, 1);    
    
    /* Update global (driver) data structures. */
    spin_lock_irqsave(&driver_lock, flags);
    minor_status[idx] = MINOR_FREE;
    crc_devices[idx] = NULL;
    device_status[idx] = WORKING;
    spin_unlock_irqrestore(&driver_lock, flags);

    printk(KERN_INFO "Device (minor %d) successfully removed.\n", idx);
}


/* */
static void __exit crcdev_exit_module(void)
{
    unsigned long flags;

    spin_lock_irqsave(&driver_lock, flags);
    driver_status = REMOVE_PENDING;
    spin_unlock_irqrestore(&driver_lock, flags);

    pci_unregister_driver(&crcdev_driver);
    class_destroy(crcdev_class);
    
    printk(KERN_NOTICE "Driver successfully removed.\n");
}

module_init(crcdev_init_module);
module_exit(crcdev_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Grzegorz Kołakowski");

