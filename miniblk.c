#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/kthread.h>
#include <linux/bio.h>
#include "miniblk.h"

#include "bio_helper.h"
#include "syncer.h"
#include "buffer.h"
#include "srl.h"

struct minidev *minidev;
static struct bio_wrapper_list *bio_wrapper_list;

static struct block_device_operations minidev_fops = {
	.owner = THIS_MODULE,
};

//struct block_device *blkdev_get_by_path(const char *path, fmode_t mode, void *holder)
//{
//        struct block_device *bdev;
//        int err;
//
//        bdev = lookup_bdev(path);
//        if (IS_ERR(bdev))
//                return bdev;
//
//        err = blkdev_get(bdev, mode);
//        if (err)
//                return ERR_PTR(err);
//
//        if ((mode & FMODE_WRITE) && bdev_read_only(bdev)) {
//                blkdev_put(bdev, mode);
//                return ERR_PTR(-EACCES);
//        }
//
//        return bdev;
//}

int wrapper_run(void *data)
{
        struct bio_wrapper *bio_wrapper;
        uint64_t count = 0;

        while(!kthread_should_stop()) {
                while ((bio_wrapper = bio_wrapper_list_get(bio_wrapper_list)) == NULL) {
                        wait_for_completion(&bio_wrapper_list->complete);
                        if (kthread_should_stop()) {
                                return 0;
                        }
                }

                if (bio_data_dir(bio_wrapper->bio) == WRITE) {
                        bio_wrapper_add_meta();
                }

                pr_info("get wrapper ok, count:%llu|bio:%p\n", ++count, bio_wrapper->bio);

                submit_bio_list(&bio_wrapper->bio_list);
	}

        return 0;
}

static int minidev_make_request(struct request_queue *q, struct bio *bio)
{
        int ret;
        static uint64_t count = 0;
        struct bio_wrapper *wrapper;

        wrapper = init_bio_wrapper(bio, hadm_bio_end_io);
        if (wrapper == NULL) {
                bio_endio(bio, -EIO);
                pr_info("wrapper init failed.\n");
        }

	ret = bio_wrapper_list_put(bio_wrapper_list, wrapper);
        if (ret < 0) {
                bio_endio(bio, -EIO);
                pr_info("bio_wrapper_list full.\n");
        }
        pr_info("make request handler count: %llu.bio:%p\n", ++count, bio);

	return 0;
}

static int init_minidev(struct minidev *dev, struct srl *srl)
{
	int ret = 0;
	struct request_queue *q;
	struct gendisk *d;
	struct block_device *bdev;
	dev_t major;

	major = register_blkdev(0, MINIDEV_NAME);
	if(major < 0) {
		return major;
	}
	dev->major = major;

	q = blk_alloc_queue(GFP_KERNEL);
	if(!q) {
		ret = -ENOMEM;
		goto err_queue;
	}
	blk_queue_make_request(q, minidev_make_request);
	dev->rq = q;

        d = alloc_disk(1);
        if(!d) {
                ret = -ENOMEM;
                goto err_disk;
        }

        strncpy(d->disk_name, MINIDEV_NAME, strlen(MINIDEV_NAME));
        d->major = major;
        d->first_minor = 0;
        d->fops = &minidev_fops;
        d->queue = q;
        dev->disk = d;

        bdev = blkdev_get_by_path(MINIDEV_BDEV, FMODE_READ | FMODE_WRITE, dev);
        if(IS_ERR(bdev)) {
                printk(KERN_INFO "open bdev fail!");
                ret = -EBUSY;
                goto err_bdev;
        }
        set_capacity(dev->disk, i_size_read(bdev->bd_inode) >> 9);
        dev->bdev = bdev;
        dev->srl = srl;

        return 0;

err_bdev:
        put_disk(minidev->disk);

err_disk:
        blk_cleanup_queue(q);

err_queue:
	unregister_blkdev(major, MINIDEV_NAME);
	return ret;
}

/* FIXME free mem */
static int __init minidev_init(void)
{
	int ret = 0;
        struct task_struct *task;
        struct srl *srl = NULL;
	struct data_buffer *buffer;

        bio_wrapper_list = init_bio_wrapper_list(MAX_BIO_WRAPPER_LIST_SIZE);
        if (bio_wrapper_list == NULL) {
                goto fail;
        }

        srl = init_srl(SRLDEV_BDEV, minidev);
        if (srl == NULL) {
                return -ENOMEM;
        }

	buffer = init_data_buffer(MAX_BUFFER_SIZE);
	if (buffer == NULL) {
		return -1;
	}

	minidev = kzalloc(sizeof(struct minidev), GFP_KERNEL);
	if(!minidev) {
		ret = -ENOMEM;
                goto fail;
	}

        pr_info("ready init minidev.\n");
        /* FIXME */
	ret = init_minidev(minidev, srl);
	if(ret) {
		goto fail;
	}
	minidev->buffer = buffer;

	add_disk(minidev->disk);
	printk(KERN_INFO "miniblk init!\n");
        pr_info("disk bdev:%p, srl bdev: %p.\n",
                        minidev->bdev, minidev->srl->bdev);

        task = kthread_run(wrapper_run, NULL, "wrapper");
        if (task == NULL || IS_ERR(task)) {
                goto fail;
        }
        minidev->task = task;

	task = kthread_run(syncer_run, NULL, "syncer");
        if (task == NULL || IS_ERR(task)) {
                goto fail;
        }
        minidev->syncer = task;

	return 0;

fail:
        kfree(srl);
	kfree(minidev);
	return ret;
}

static void __exit minidev_exit(void)
{
        complete(&bio_wrapper_list->complete);
        kthread_stop(minidev->task);
        kthread_stop(minidev->syncer);

	msleep(1000);
	del_gendisk(minidev->disk);
	put_disk(minidev->disk);

	blkdev_put(minidev->bdev, FMODE_READ | FMODE_WRITE);

	blk_cleanup_queue(minidev->rq);

	unregister_blkdev(minidev->major, MINIDEV_NAME);

	msleep(2000);
	free_data_buffer(minidev->buffer);
	kfree(minidev);

	printk(KERN_INFO "miniblk exit!\n");
}

module_init(minidev_init);
module_exit(minidev_exit);
MODULE_LICENSE("GPL");
