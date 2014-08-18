#include "syncer.h"
#include <linux/kthread.h>

extern struct minidev *minidev;

void sync_read_endio(struct bio *bio, int err)
{
	struct completion *compl = bio->bi_private;

	complete(compl);
	if (err) {
		BUG();
	}
}

void sync_write_endio(struct bio *bio, int err)
{
	if (err == 0) {
		srl_head_inc(minidev->srl);
	}
	pr_info("write data to disk finish head:%lu.\n",
			srl_head(minidev->srl));
}

int sync_sector(struct srl *srl, sector_t start)
{
	struct meta * meta;
	struct bio *rbio, *wbio;
	DECLARE_COMPLETION_ONSTACK(compl);

	rbio = bio_alloc(GFP_KERNEL, 2);
	if(rbio == NULL) {
		return -1;
	}

	rbio->bi_bdev = srl->bdev;
	rbio->bi_rw = READ;
	rbio->bi_private = &compl;
	rbio->bi_sector = start;
	rbio->bi_end_io = sync_read_endio;

	if (bio_add_srl_data(rbio, 2) < 0) {
		goto err_rbio;
	}

	generic_make_request(rbio);

	wait_for_completion(&compl);    /* handle bio err? */

	meta = (struct meta *)page_address(rbio->bi_io_vec[0].bv_page);
	//pr_content(meta, 8192);

	wbio = bio_alloc(GFP_KERNEL, 1);
	if (wbio == NULL) {
		goto err_rbio;
	}

	wbio->bi_bdev = minidev->bdev;
	wbio->bi_rw = WRITE;
	wbio->bi_sector = meta->disk_sector;
	wbio->bi_end_io = sync_write_endio;

	if (bio_add_page(wbio, rbio->bi_io_vec[1].bv_page,
				PAGE_SIZE, 0) != PAGE_SIZE) {
		goto err_wbio;
	}

	pr_info("write data to disk. sector:%lu.\n",
			wbio->bi_sector);
	generic_make_request(wbio);

	__free_page(rbio->bi_io_vec[0].bv_page); 	/* free vec[1].bv_page is ok? add_page will inc page count? No */
	bio_put(rbio);
	return 0;

err_wbio:
	bio_free_pages(wbio);
	bio_put(wbio);
err_rbio:
	bio_free_pages(rbio);
	bio_put(rbio);
	return -1;
}

int syncer_run(void *arg)
{
	sector_t start, tail;

	pr_info("syncer_run...\n");

	while(!kthread_should_stop()) {
		start = srl_head(minidev->srl);
		tail = srl_tail(minidev->srl);
		pr_info("ready syncer %lu - %lu.\n",
				start, tail);

		while(start < tail) {
			sync_sector(minidev->srl, start);
			start += (PAGE_SIZE + META_SIZE) >> BLK_SHIFT;
		}

		msleep(1000);
	}

	msleep(2000);		/* just wait for io completion */
	return 0;
}
