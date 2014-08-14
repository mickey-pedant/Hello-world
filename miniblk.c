#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/kthread.h>
#include <linux/bio.h>
#include "miniblk.h"
#include "bio_helper.h"

static struct minidev *minidev;
static struct bio_wrapper_list *bio_wrapper_list;

static struct block_device_operations minidev_fops = {
	.owner = THIS_MODULE,
};

int wrapper_run(void *data)
{
        struct bio_wrapper *bio_wrapper;
        uint64_t count = 0;

        while(!kthread_should_stop()) {
                while ((bio_wrapper = bio_wrapper_list_get(bio_wrapper_list)) == NULL) {
                        pr_info("wait begin:------------\n");
                        wait_for_completion(&bio_wrapper_list->complete);
                        pr_info("wait end:------------\n");
                        if (kthread_should_stop()) {
                                return 0;
                        }
                }

                // dump_wrapper_list(bio_wrapper_list, __FUNCTION__);
                // dump_bio_wrapper(bio_wrapper->bio);
                if (bio_data_dir(bio_wrapper->bio) == WRITE) {
                        bio_wrapper_add_meta();
                }

                if (!count) {
                        msleep(1000);
                }
                pr_info("get wrapper ok, count:%llu|bio:%p\n", ++count, bio_wrapper->bio);

                submit_bio_list(&bio_wrapper->bio_list);
                // bio_wrapper->bio->bi_bdev = minidev->bdev;
                // generic_make_request(bio_wrapper->bio);
                // free_bio_wrapper(bio_wrapper);
                // msleep(100);
        }

        return 0;
}


static int minidev_make_request(struct request_queue *q, struct bio *bio)
{
        int ret;
        static uint64_t count = 0;
        struct bio_wrapper *wrapper;

        //dump_bio(bio, __FUNCTION__);
        wrapper = init_bio_wrapper(bio, hadm_bio_end_io, minidev->bdev);
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

        //dump_wrapper_list(bio_wrapper_list, __FUNCTION__);
        //free_bio_wrapper(wrapper);
	// invalidate_mapping_pages(minidev->bdev->bd_inode->i_mapping, 0, -1);
	// dump_bio(bio);
        //bio->bi_bdev = minidev->bdev;
        //generic_make_request(bio);
        return 0;
}

static int init_minidev(struct minidev *dev)
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

        return 0;

err_bdev:
        put_disk(minidev->disk);

err_disk:
        blk_cleanup_queue(q);

err_queue:
	unregister_blkdev(major, MINIDEV_NAME);
	return ret;
}

static int __init minidev_init(void)
{
	int ret = 0;
        struct task_struct *task;

        bio_wrapper_list = init_bio_wrapper_list(MAX_BIO_WRAPPER_LIST_SIZE);
        if (bio_wrapper_list == NULL) {
                goto fail;
        }

	minidev = kzalloc(sizeof(struct minidev), GFP_KERNEL);
	if(!minidev) {
		return -ENOMEM;
	}

        pr_info("ready init minidev.\n");
	ret = init_minidev(minidev);
	if(ret) {
		goto fail;
	}

	add_disk(minidev->disk);
	printk(KERN_INFO "miniblk init!\n");

        task = kthread_run(wrapper_run, NULL, "wrapper_worker");
        if (task == NULL || IS_ERR(task)) {
                goto fail;
        }
        minidev->task = task;

	return 0;

fail:
	kfree(minidev);
	return ret;
}

static void __exit minidev_exit(void)
{
	del_gendisk(minidev->disk);
	put_disk(minidev->disk);

	blkdev_put(minidev->bdev, FMODE_READ | FMODE_WRITE);

	blk_cleanup_queue(minidev->rq);

	unregister_blkdev(minidev->major, MINIDEV_NAME);

	kfree(minidev);

        complete(&bio_wrapper_list->complete);
        kthread_stop(minidev->task);

	printk(KERN_INFO "miniblk exit!\n");
}

module_init(minidev_init);
module_exit(minidev_exit);
MODULE_LICENSE("GPL");
