#ifndef __MINIBLK__
#define __MINIBLK__

#include <linux/blkdev.h>
#include <net/sock.h>

#define MINIDEV_NAME		"minibd"
#define MINIDEV_BDEV		"/dev/sda"

struct minidev {
        struct task_struct *task;
	struct gendisk *disk;
	struct request_queue *rq;
	dev_t major;
	struct block_device *bdev;
};

#endif // __HABD_H__
