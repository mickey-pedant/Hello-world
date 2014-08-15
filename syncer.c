#include "bio_helper.h"
#include "miniblk.h"
#include "syncer.h"

void sync_read_endio(struct bio *bio, int err)
{
        complete(bio->private);
        bio_put(bio);
}

int sync_sector(struct srl *srl, sector_t start)
{
        struct bio *bio_src, bio_dst;
        DECLARE_COMPLETION_ONSTACK(compl);

        bio = bio_alloc_with_pages(GFP_KERNEL, 2);
        if(bio == NULL) {
                goto err_bio;
        }

        bio->bi_bdev = srl->bdev;
        bio->bi_rw = READ;
        bio->bi_private = &compl;
        bio->bi_sector = start;
        bio->bi_end_io = sync_read_endio;

        generic_make_request(bio);

        wait_for_completion(&compl);

        meta = (struct meta *)page_address(bio->bi_io_vec[0].bv_page);

        bio = bio_alloc(GFP_KERNEL, 1);
        if (bio == NULL) {
                goto err_bio;
        }

        bio->bi_bdev = minidev->bdev;
        bio->bi_rw = WRITE;
        bio->bi_sector = meta->disk_sector;
        bio->






int syncer_run(void *arg)
{
        sector_t start, tail;

        pr_info("syncer_run...\n");

        while(!kthread_should_stop()) {
                start = srl_head(minidev->srl);
                tail = srl_tail(minidev->srl);

                while(start < tail) {
                        sync_sector(minidev->srl, start++);
                        srl_head_inc(minidev->srl);
                }

                msleep(1000);
        }

        return 0;
}
