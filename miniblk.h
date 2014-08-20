#ifndef __MINIBLK__
#define __MINIBLK__

#include <linux/blkdev.h>
#include <net/sock.h>

#define MINIDEV_NAME		"minibd"
#define MINIDEV_BDEV		"/dev/sdc"
#define SRLDEV_BDEV             "/dev/sdb"
#define MAX_BUFFER_SIZE (1 << 1)
struct buffer;

struct srl {
        atomic64_t head;
        atomic64_t tail;
        spinlock_t lock;
        struct block_device *bdev;
};

struct minidev {
        struct task_struct *task;
	struct task_struct *syncer;
	struct gendisk *disk;
	struct request_queue *rq;
	dev_t major;
	struct block_device *bdev;
        struct srl *srl;
	struct data_buffer *buffer;
};

#endif // __HABD_H__
