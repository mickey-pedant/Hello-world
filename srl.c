#include <linux/blkdev.h>

#include "srl.h"

struct srl *init_srl(const char *disk, void *owner)
{
        struct srl *srl;
        struct block_device *bdev;

        srl = kzalloc(sizeof(struct srl), GFP_KERNEL);
        if (srl == NULL)
                return NULL;

        bdev = blkdev_get_by_path(disk, FMODE_READ | FMODE_WRITE, owner);
        if(IS_ERR(bdev)) {
                printk(KERN_INFO "open bdev fail!");
                goto err_bdev;
        }
        srl->bdev = bdev;
	spin_lock_init(&srl->lock);
	sema_init(&srl->sema, 0);
        atomic64_set(&srl->tail, 0);
        atomic64_set(&srl->disk_tail, 0);
        atomic64_set(&srl->head, 0);

        return srl;
err_bdev:
        kfree(srl);
        return NULL;
}

sector_t srl_tail(struct srl *srl)
{
	return atomic64_read(&srl->tail);
}

sector_t srl_disk_tail(struct srl *srl)
{
	return atomic64_read(&srl->disk_tail);
}

sector_t srl_head(struct srl *srl)
{
	return atomic64_read(&srl->head);
}

void srl_tail_inc(struct srl *srl)
{
	atomic64_add(9, &srl->tail);
}

void srl_disk_tail_inc(struct srl *srl)
{
	atomic64_add(9, &srl->disk_tail);
	up(&srl->sema);
}

void srl_head_inc(struct srl *srl)
{
	atomic64_add(9, &srl->head);
}
