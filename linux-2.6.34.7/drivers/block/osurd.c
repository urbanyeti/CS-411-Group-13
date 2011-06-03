 /* CS 411 Project 4 Team 13
  * Lewis Valentine, Daniel Urbanski, Dwight Trahin, James Admire
  * osurd.c
  * A simple ram disk driver.
  * Code is modified from the 2.6.31.13 version of the sbull driver
  * from http://www.cs.fsu.edu/~baker/devices/lxr/http/source/ldd-examples/sbull/sbull.c
  */

  /* Working on getting the code to compile.  I removed the request modes and a
   * a lot of the extra request functions because they are unneccessary and harder
   * to write in the newer version of the kernel. Someone could add in the module
   * params and add checks to the setup function. Someone else might start adding
   * in printk stmts.
   */

  /* This version still has a lot changes that need to be made to it.  We would have to
   * change the request stuff and the inode stuff.  The link at the end should help.
   * It also has a lot of extra code that we would have to go through and probably
   * change.  It has a lot of code already written on the other hand.  We may want to
   * use the code from http://blog.superpat.com/2010/05/04/a-simple-block-driver-for-linux-kernel-2-6-31/
   * instead.
   */


 /*
  * Sample disk driver, from the beginning.
  */

 #include <linux/module.h>
 #include <linux/moduleparam.h>
 #include <linux/init.h>

 #include <linux/sched.h>
 #include <linux/kernel.h>       /* printk() */
 #include <linux/slab.h>         /* kmalloc() */
 #include <linux/fs.h>           /* everything... */
 #include <linux/errno.h>        /* error codes */
 #include <linux/timer.h>
 #include <linux/types.h>        /* size_t */
 #include <linux/fcntl.h>        /* O_ACCMODE */
 #include <linux/hdreg.h>        /* HDIO_GETGEO */
 #include <linux/kdev_t.h>
 #include <linux/vmalloc.h>
 #include <linux/genhd.h>
 #include <linux/blkdev.h>
 #include <linux/buffer_head.h>  /* invalidate_bdev */
 #include <linux/bio.h>

 MODULE_LICENSE("Dual BSD/GPL");

 static int osurd_major = 0;
 module_param(osurd_major, int, 0);
 static int hardsect_size = 512;
 module_param(hardsect_size, int, 0);
 static int nsectors = 1024;     /* How big the drive is */
 module_param(nsectors, int, 0);
 static int ndevices = 4;
 module_param(ndevices, int, 0);


 /*
  * Minor number and partition management.
  */
 #define OSURD_MINORS    16
 #define MINOR_SHIFT     4
 #define DEVNUM(kdevnum) (MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT

 /*
  * We can tweak our hardware sector size, but the kernel talks to us
  * in terms of small sectors, always.
  */
 #define KERNEL_SECTOR_SIZE      512

 /*
  * After this much idle time, the driver will simulate a media change.
  */
 #define INVALIDATE_DELAY        30*HZ

 /*
  * The internal representation of our device.
  */
 struct osurd_dev {
         int size;                       /* Device size in sectors */
         u8 *data;                       /* The data array */
         short users;                    /* How many users */
         short media_change;             /* Flag a media change? */
         spinlock_t lock;                /* For mutual exclusion */
         struct request_queue *queue;    /* The device request queue */
         struct gendisk *gd;             /* The gendisk structure */
         struct timer_list timer;        /* For simulated media changes */
 };

 static struct osurd_dev *Devices = NULL;

 /*
  * Handle an I/O request.
  */
 static void osurd_transfer(struct osurd_dev *dev, sector_t sector,
                 unsigned long nsect, char *buffer, int write)
 {
         unsigned long offset = sector*KERNEL_SECTOR_SIZE;
         unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;

         if ((offset + nbytes) > dev->size) {
                 printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
                 return;
         }
         if (write)
                 memcpy(dev->data + offset, buffer, nbytes);
         else
                 memcpy(buffer, dev->data + offset, nbytes);
 }

 /*
  * The simple form of the request function.
  */
 static void osurd_request(struct request_queue *q)
 {
         struct request *req;

         while ((req = blk_fetch_request(q)) != NULL) {
                 struct osurd_dev *dev = req->rq_disk->private_data;
                 if (! blk_fs_request(req)) {
                         printk (KERN_NOTICE "Skip non-fs request\n");
                         blk_end_request(req, -EIO, blk_rq_cur_sectors(req));
                         continue;
                 }
     //          printk (KERN_NOTICE "Req dev %d dir %ld sec %ld, nr %d f %lx\n",
     //                          dev - Devices, rq_data_dir(req),
     //                          req->sector, req->current_nr_sectors,
     //                          req->flags);
                 osurd_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req),
                                 req->buffer, rq_data_dir(req));
                 blk_end_request(req, 1, blk_rq_cur_sectors(req));
         }
 }

 /*
  * Open and close.
  */

 static int osurd_open(struct block_device *bdev, fmode_t mode)
 {
         struct osurd_dev dev = bdev->bd_disk->private_data;
         del_timer_sync(&dev->timer);
         spin_lock(&dev->lock);
         if (! dev->users)
                 check_disk_change(bdev);
         dev->users++;
         spin_unlock(&dev->lock);
         return 0;
 }

 static int osurd_release(struct gendisk *gd, fmode_t mode)
 {
         struct osurd_dev *dev = gd->private_data;

         spin_lock(&dev->lock);
         dev->users--;

         if (!dev->users) {
                 dev->timer.expires = jiffies + INVALIDATE_DELAY;
                 add_timer(&dev->timer);
         }
         spin_unlock(&dev->lock);

         return 0;
 }

 /*
  * Look for a (simulated) media change.
  */
 int osurd_media_changed(struct gendisk *gd)
 {
         struct osurd_dev *dev = gd->private_data;

         return dev->media_change;
 }

 /*
  * Revalidate.  WE DO NOT TAKE THE LOCK HERE, for fear of deadlocking
  * with open.  That needs to be reevaluated.
  */
 int osurd_revalidate(struct gendisk *gd)
 {
         struct osurd_dev *dev = gd->private_data;

         if (dev->media_change) {
                 dev->media_change = 0;
                 memset (dev->data, 0, dev->size);
         }
         return 0;
 }

 /*
  * The "invalidate" function runs out of the device timer; it sets
  * a flag to simulate the removal of the media.
  */
 void osurd_invalidate(unsigned long ldev)
 {
         struct osurd_dev *dev = (struct osurd_dev *) ldev;

         spin_lock(&dev->lock);
         if (dev->users || !dev->data)
                 printk (KERN_WARNING "osurd: timer sanity check failed\n");
         else
                 dev->media_change = 1;
         spin_unlock(&dev->lock);
 }

 /*
  * The ioctl() implementation
  */

 int osurd_ioctl (struct block_device *bdev, fmode_t mode,
                  unsigned int cmd, unsigned long arg)
 {
         long size;
         struct hd_geometry geo;
         struct osurd_dev dev = bdev->bd_disk->private_data;

         switch(cmd) {
             case HDIO_GETGEO:
                 /*
                  * Get geometry: since we are a virtual device, we have to make
                  * up something plausible.  So we claim 16 sectors, four heads,
                  * and calculate the corresponding number of cylinders.  We set the
                  * start of data at sector four.
                  */
                 size = dev->size*(hardsect_size/KERNEL_SECTOR_SIZE);
                 geo.cylinders = (size & ~0x3f) >> 6;
                 geo.heads = 4;
                 geo.sectors = 16;
                 geo.start = 4;
                 if (copy_to_user((void __user *) arg, &geo, sizeof(geo)))
                         return -EFAULT;
                 return 0;
         }

         return -ENOTTY; /* unknown command */
 }

int osurd_getgeo(struct block_device * bdev, struct hd_geometry * geo) {
	long size;
	struct osurd_dev dev = bdev->bd_disk->private_data;

	/* We have no real geometry, of course, so make something up. */
	size = dev->size*(hardsect_size/KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 4;
	return 0;
}


 /*
  * The device operations structure.
  */
 static struct block_device_operations osurd_ops = {
         .owner           = THIS_MODULE,
         .open            = osurd_open,
         .release         = osurd_release,
         .media_changed   = osurd_media_changed,
         .revalidate_disk = osurd_revalidate,
         .ioctl           = osurd_ioctl
         .getgeo          = osurd_getgeo
 };


 /*
  * Set up our internal device.
  */
 static void setup_device(struct osurd_dev *dev, int which)
 {
         /* Check params?
          * Hardsect % 512 should be zero and > 512
          * Disksize % 512 should be zero?
          * Disksize % Hardsect should be zero (to get right number of sectors)
          */

         /*
          * Get some memory.
          */
         memset (dev, 0, sizeof (struct osurd_dev));
         dev->size = nsectors*hardsect_size;
         dev->data = vmalloc(dev->size);
         if (dev->data == NULL) {
                 printk (KERN_NOTICE "vmalloc failure.\n");
                 return;
         }
         spin_lock_init(&dev->lock);

         /*
          * The timer which "invalidates" the device.
          */
         init_timer(&dev->timer);
         dev->timer.data = (unsigned long) dev;
         dev->timer.function = osurd_invalidate;

         /*
          * The I/O queue, depending on whether we are using our own
          * make_request function or not.
          */

         dev->queue = blk_init_queue(osurd_request, &dev->lock);
         if (dev->queue == NULL)
         goto out_vfree;
         blk_queue_logical_block_size(dev->queue, hardsect_size);
         dev->queue->queuedata = dev;
         /*
          * And the gendisk structure.
          */
         dev->gd = alloc_disk(OSURD_MINORS);
         if (! dev->gd) {
                 printk (KERN_NOTICE "alloc_disk failure\n");
                 goto out_vfree;
         }
         dev->gd->major = osurd_major;
         dev->gd->first_minor = which*OSURD_MINORS;
         dev->gd->fops = &osurd_ops;
         dev->gd->queue = dev->queue;
         dev->gd->private_data = dev;
         snprintf (dev->gd->disk_name, 32, "osurd%c", which + 'a');
         set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
         add_disk(dev->gd);
         return;

   out_vfree:
         if (dev->data)
                 vfree(dev->data);
 }



 static int __init osurd_init(void)
 {
         int i;
         /*
          * Get registered.
          */
         osurd_major = register_blkdev(osurd_major, "osurd");
         if (osurd_major <= 0) {
                 printk(KERN_WARNING "osurd: unable to get major number\n");
                 return -EBUSY;
         }
         /*
          * Allocate the device array, and initialize each one.
          */
         Devices = kmalloc(ndevices*sizeof (struct osurd_dev), GFP_KERNEL);
         if (Devices == NULL)
                 goto out_unregister;
         for (i = 0; i < ndevices; i++)
                 setup_device(Devices + i, i);

         return 0;

   out_unregister:
         unregister_blkdev(osurd_major, "osurd");
         return -ENOMEM;
 }

 static void osurd_exit(void)
 {
         int i;

         for (i = 0; i < ndevices; i++) {
                 struct osurd_dev *dev = Devices + i;

                 del_timer_sync(&dev->timer);
                 if (dev->gd) {
                         del_gendisk(dev->gd);
                         put_disk(dev->gd);
                 }
                 if (dev->queue) {
                         blk_cleanup_queue(dev->queue);
                 }
                 if (dev->data)
                         vfree(dev->data);
         }
         unregister_blkdev(osurd_major, "osurd");
         kfree(Devices);
 }

 module_init(osurd_init);
 module_exit(osurd_exit);
