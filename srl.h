#ifndef __SRL_H_
#define __SRL_H_

#include <linux/kernel.h>
#include <linux/semaphore.h>

struct srl {
        atomic64_t head;
        atomic64_t tail;
	atomic64_t disk_tail;
	struct semaphore sema;
        spinlock_t lock;
        struct block_device *bdev;
};

struct srl *init_srl(const char *disk, void *owner);

sector_t srl_tail(struct srl *srl);
sector_t srl_head(struct srl *srl);
sector_t srl_disk_tail(struct srl *srl);
void srl_tail_inc(struct srl *srl);
void srl_disk_tail_inc(struct srl *srl);
void srl_head_inc(struct srl *srl);

#endif		/* __SRL_H_ */
