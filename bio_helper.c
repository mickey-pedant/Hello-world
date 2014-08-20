#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/kthread.h>
#include <linux/bio.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/bio.h>

#include "bio_helper.h"
#include "buffer.h"

extern struct minidev *minidev;

void hadm_bio_end_io(struct bio *bio, int err)
{
	void *src;
	struct bio_wrapper *bio_w;
	struct srl_data *srl_data;
	struct page *page;
	struct bio_struct *bio_struct;
	static unsigned long count = 0;

	bio_struct = (struct bio_struct *)bio->bi_private;
	bio_w = bio_struct->wrapper;

	pr_info("handler bio READ begin");

	pr_info("handler bio READ end");

	if (err) {
		bio_w->err |= err;
		buffer_inuse_del_occd(minidev->buffer);
	} else if(bio_data_dir(bio_w->bio) == READ) {
		pr_info("copy bio data\n");
		//pr_c_content(page_address(bio->bi_io_vec[0].bv_page), 512);
		//memset(page_address(bio_w->bio->bi_io_vec[0].bv_page), 'C', PAGE_SIZE);
		src = page_address(bio_w->bio->bi_io_vec[bio_struct->idx].bv_page);
		if (src == NULL) {
			pr_info("BUG!!!!!!!!!! src is NULL.");
		} else {
			memcpy(page_address(bio_w->bio->bi_io_vec[bio_struct->idx].bv_page),
					page_address(bio->bi_io_vec[0].bv_page), PAGE_SIZE);
		}
		//memcpy(page_address(bvec->bv_page), page_address(bio->bi_io_vec[0].bv_page), PAGE_SIZE);
	} else {
		page = bio->bi_io_vec[1].bv_page;
		get_page(page);
		srl_data = (struct srl_data *)bio_struct->private;
		srl_data->data_page = page;

		srl_disk_tail_inc(minidev->srl);

		/* guarantee in submit phase */
		pr_info("add buffer %lu times.\n", ++count);
		buffer_data_add_occd(minidev->buffer, srl_data);
	}

	if (atomic_dec_and_test(&bio_w->count)) {
		pr_info("----------------------------XXbio wrapper endio.");
		bio_endio(bio_w->bio, bio_w->err);
		free_bio_wrapper(bio_w);
	}

	pr_info("===== hadm_bio_end_io =====");
}

void hadm_bio_list_free(struct list_head *bio_list)
{
	struct bio_struct *bio_struct;
	struct bio_struct *temp;

	list_for_each_entry_safe(bio_struct, temp, bio_list, list) {
		list_del(&bio_struct->list);
		free_bio_struct(bio_struct);
	}
}

int hadm_bio_split(struct bio_wrapper *wrapper, bio_end_io_t *bi_end_io)
{
	struct list_head *bio_list = &wrapper->bio_list;
	//struct bio_list *bio_list;
	struct srl_data *srl_data = NULL;
	struct bio_struct *bio_struct;
	struct bio *bio;
	struct bio *bio_src;
	struct bio_vec *bvec;
	struct page *page;
	void *addr;
	void *addr_src;
	int i;

	//	bio_list = kmalloc(GFP_KERNEL, sizeof(struct bio_list));
	//	if(bio_list == NULL) {
	//		return NULL;
	//	}
	//	bio_list_init(bio_list);

	bio_src = wrapper->bio;
	bio_for_each_segment(bvec, bio_src, i) {
		//pr_info("---------------split bio--------------------\n");
		bio = bio_alloc(GFP_NOIO, 1);
		if(bio == NULL) {
			goto err_bio;
		}

		bio->bi_bdev = bio_src->bi_bdev;
		bio->bi_flags = bio_src->bi_flags;
		bio->bi_rw = bio_src->bi_rw;
		bio->bi_end_io = bi_end_io;
		bio->bi_sector = bio_src->bi_sector + 8 * i;

		if (bio_data_dir(bio) == WRITE) {
			if (bio_add_meta_page(bio) != 0) {
				pr_info("add meta failed.\n");
				bio_put(bio);
				goto err_bio;
			}

			/*
			 * add a srl_data struct for write bio. at this time, the
			 * data's page is NULL, will add in endio
			 */
			srl_data = init_srl_data(srl_tail(minidev->srl),
					bio->bi_sector, NULL);
			if (srl_data == NULL) {
				goto err_bio;
			}

			bio->bi_bdev = minidev->srl->bdev;
			bio->bi_sector = srl_tail(minidev->srl);
			/* srl tail increase in the endio! sync model*/
			srl_tail_inc(minidev->srl);
			pr_info("write srl: srl sector:%lu.\n", bio->bi_sector);
		} else {
			bio->bi_bdev = minidev->bdev;
		}

		page = alloc_page(GFP_KERNEL);
		if(page == NULL) {
			bio_free_pages(bio);
			bio_put(bio);
			goto err_bio;
		}

		addr = page_address(page);
		addr_src = page_address(bvec->bv_page);
		memcpy(addr, addr_src, PAGE_SIZE);

		if(bio_add_page(bio, page, bvec->bv_len, bvec->bv_offset) == 0) {
			__free_page(page);
			bio_free_pages(bio);
			bio_put(bio);
			goto err_bio;
		}

		//dump_bio(bio, __FUNCTION__);
		bio_struct = init_bio_struct(bio, wrapper, srl_data, i);
		if (bio_struct == NULL) {
			bio_free_pages(bio);
			bio_put(bio);
			goto err_bio;
		}
		bio->bi_private = bio_struct;

		list_add_tail(&bio_struct->list, &wrapper->bio_list);
		//pr_info("---------------split bio end-------");
		//bio_list_add(bio_list, bio);
	}

	pr_info("after split bio, wrapper count:%d.\n", atomic_read(&wrapper->count));
	list_for_each_entry(bio_struct, &wrapper->bio_list, list) {
		pr_info("->%p", bio_struct->bio);
	}

	return 0;

err_bio:
	hadm_bio_list_free(bio_list);

	return -1;
}

void hadm_bio_list_dump(struct bio_list *bio_list)      /* FIXME */
{
	struct bio_vec *bvec;
	int i;

	struct bio *bio = bio_list->head;

	pr_info("xxxx bio = %p", bio);
	while(bio != NULL) {
		pr_info("=============");
		pr_info("bio->bi_sector = %lu", (unsigned long)bio->bi_sector);
		pr_info("bio->bi_vcnt = %u", bio->bi_vcnt);
		bio_for_each_segment(bvec, bio, i) {
			pr_info("bvec->bv_page = %p", bvec->bv_page);
		}

		bio = bio->bi_next;
		pr_info("=============");
	}
}

struct bio_wrapper *alloc_bio_wrapper(void)
{
	struct bio_wrapper *bio_w = kzalloc(GFP_KERNEL, sizeof(struct bio_wrapper));

	if(bio_w == NULL) {
		return NULL;
	}

	INIT_LIST_HEAD(&bio_w->bio_list);
	INIT_LIST_HEAD(&bio_w->list);

	return bio_w;
}

void free_bio_wrapper(struct bio_wrapper *bio_w)
{
	hadm_bio_list_free(&bio_w->bio_list);
	kfree(bio_w);
}

void submit_bio_list(struct list_head *bio_list)
{
	struct bio *bio;
	struct bio_struct *bio_struct;
	struct srl_data *buffer_data;
	void *src_addr;
	//static unsigned long count = 0;

	/* FIXME buffer full? at least one bio in the list */
	list_for_each_entry(bio_struct, bio_list, list) {
		//pr_info("===== submit bio =====");

		bio = bio_struct->bio;
		//pr_info("submit list bio: %p\n", bio);
		dump_bio(bio, __FUNCTION__);
		//		if (bio_data_dir(bio) == WRITE) {
		//			pr_content(page_address(bio->bi_io_vec[0].bv_page), 512);
		//			pr_c_content(page_address(bio->bi_io_vec[1].bv_page), 512);
		//		}
		schedule();             /* FIXME XXOOXX */
		//msleep(10);

		if (bio_data_dir(bio) == READ) {
			buffer_data = get_find_data(minidev->buffer, bio->bi_sector);
			if (buffer_data != NULL) {
				pr_info("find data in buffer.!!!!!!\n");
				src_addr = page_address(buffer_data->data_page);
//				pr_c_content(src_addr, 512);
//				//dst_addr = page_address(bio->bi_io_vec[0].bv_page);
//				memcpy(dst_addr, src_addr, PAGE_SIZE);
//
//				if (atomic_dec_and_test(&bio_wrapper->count)) {
//					bio_endio(bio_w->bio, 0);
//				}
//				bio = bio->bi_next;
//				continue;
			} else {
				pr_info("not find the data.");
			}

		} else {
			//pr_info("submit write bio %lu times.\n", ++count);
			buffer_inuse_pre_occu(minidev->buffer);
		}

		//dump_bio(bio, __FUNCTION__);
		submit_bio(bio->bi_rw, bio);
	}
	// hadm_bio_list_free(bio_list);
	// bio_endio(bio_w->bio, 0);       /* endio? */
}

struct bio_wrapper_list *init_bio_wrapper_list(uint64_t maxsize)
{
	struct bio_wrapper_list *bio_wrapper_list;

	bio_wrapper_list = kzalloc(sizeof(struct bio_wrapper_list), GFP_KERNEL);
	if (bio_wrapper_list == NULL) {
		return NULL;
	}

	init_completion(&bio_wrapper_list->complete);
	INIT_LIST_HEAD(&bio_wrapper_list->bio_wrapper);
	spin_lock_init(&bio_wrapper_list->lock);
	bio_wrapper_list->count = 0;
	bio_wrapper_list->maxsize = maxsize;

	return bio_wrapper_list;
}

struct bio_wrapper *bio_wrapper_list_get(struct bio_wrapper_list *bio_wrapper_list)
{
	struct bio_wrapper *bio_wrapper;

	spin_lock(&bio_wrapper_list->lock);
	if (bio_wrapper_list->count) {
		bio_wrapper = list_first_entry(&bio_wrapper_list->bio_wrapper,
				struct bio_wrapper, list);
		list_del(&bio_wrapper->list);
		bio_wrapper_list->count--;
		spin_unlock(&bio_wrapper_list->lock);
	} else {
		spin_unlock(&bio_wrapper_list->lock);
		return NULL;
	}

	return bio_wrapper;
}

int bio_wrapper_list_put(struct bio_wrapper_list *bio_wrapper_list, struct bio_wrapper *bio_wrapper)
{
	spin_lock(&bio_wrapper_list->lock);

	if (bio_wrapper_list->count == bio_wrapper_list->maxsize) {
		spin_unlock(&bio_wrapper_list->lock);
		return -1;
	}

	list_add_tail(&bio_wrapper->list, &bio_wrapper_list->bio_wrapper);
	bio_wrapper_list->count++;
	if (bio_wrapper_list->count == 1) {
		complete(&bio_wrapper_list->complete);
	}

	spin_unlock(&bio_wrapper_list->lock);

	return 0;
}

int bio_wrapper_add_meta(void)
{
	return 0;
}

/**
 * add meta page at bio->bi_io_vec[bio->bi_idx]
 */

struct bio_wrapper *init_bio_wrapper(struct bio *bio, bio_end_io_t *end_io)
{
	int ret;
	struct bio_wrapper *wrapper;

	wrapper = alloc_bio_wrapper();
	if (wrapper == NULL) {
		return NULL;
	}

	wrapper->bio = bio;
	wrapper->end_io = end_io;
	atomic_set(&wrapper->count, bio->bi_vcnt);

	ret = hadm_bio_split(wrapper, end_io);
	if (ret < 0) {
		kfree(wrapper);
		return NULL;
	}

	return wrapper;
}

void pr_content(void *addr, unsigned int size)
{
	int i;
	if (!addr) {
		return;
	}

	for (i=0; i< size/sizeof(char); i++) {
		printk("%02X ", ((char *)addr)[i]);
	}
	printk("\n");
}

void pr_c_content(void *addr, unsigned int size)
{
	int i;
	if (!addr) {
		return;
	}

	printk("pr_c_conte:\n");
	for (i=0; i< size/sizeof(char); i++) {
		printk("%c", ((char *)addr)[i]);
	}
	printk("\n");
}

void dump_bio(struct bio *bio, const char *msg)
{
	// struct bio_vec *bvec;
	// int i;

	pr_info("=========%s================", msg);
	pr_info("bio->sector = %lu", (unsigned long)bio->bi_sector);
	pr_info("bio->bi_vcnt = %u", bio->bi_vcnt);
	pr_info("bio->bi_idx = %u", bio->bi_idx);
	pr_info("bio->bi_size = %u", bio->bi_size);
	pr_info("bio->bi_bdev = %p", bio->bi_bdev);
	pr_info("bio->bi_rw = %s", bio->bi_rw & 1 ? "write" : "read");
	pr_info("=========%s=================", msg);
	/*
	   bio_for_each_segment(bvec, bio, i) {
	   pr_info("bvec index = %d", i);
	   pr_info("bvec->bv_len = %u", bvec->bv_len);
	   pr_info("bvec->bv_offset = %u", bvec->bv_offset);
	   pr_info("----------------------");
	   }
	   pr_info("=========================");
	   */
}

void __dump_bio_wrapper(struct bio *bio)
{
	// struct bio_vec *bvec;
	// int i;

	pr_info("============wrapper=============");
	pr_info("bio->sector = %lu", (unsigned long)bio->bi_sector);
	pr_info("bio->bi_vcnt = %u", bio->bi_vcnt);
	pr_info("bio->bi_idx = %u", bio->bi_idx);
	pr_info("bio->bi_size = %u", bio->bi_size);
	pr_info("=========================");
	/*
	   bio_for_each_segment(bvec, bio, i) {
	   pr_info("bvec index = %d", i);
	   pr_info("bvec->bv_len = %u", bvec->bv_len);
	   pr_info("bvec->bv_offset = %u", bvec->bv_offset);
	   pr_info("----------------------");
	   }
	   pr_info("=========================");
	   */
}

struct bio_struct *init_bio_struct(struct bio* bio, struct bio_wrapper *wrapper,
		struct srl_data *srl_data, int idx)
{
	struct bio_struct *bio_struct;

	bio_struct = kzalloc(sizeof(struct bio_struct), GFP_KERNEL);
	if (bio_struct == NULL) {
		pr_info("alloc bio_struct failed.\n");
		return NULL;
	}

	INIT_LIST_HEAD(&bio_struct->list);
	bio_struct->bio = bio;
	bio_struct->idx = idx;
	bio_struct->sector = bio->bi_sector;
	bio_struct->wrapper = wrapper;
	bio_struct->private = srl_data;

	return bio_struct;
}

void dump_wrapper_list(struct bio_wrapper_list *wrapper_list, const char *msg)
{
	struct bio_wrapper *wrapper;

	pr_info("=========%s dump start=================", msg);
	pr_info("wrapper_list:\n"
			"count: %lu, maxsize: %lu.\n",
			(unsigned long)wrapper_list->count, (unsigned long)wrapper_list->maxsize);

	list_for_each_entry(wrapper, &wrapper_list->bio_wrapper, list) {
		pr_info("--wrapper:%p, bio:%p-->", wrapper, wrapper->bio);
	}
	pr_info("=========%s dump end=================", msg);
}

void free_bio_struct(struct bio_struct *bio_struct)
{
	int i;
	struct bio *bio;
	struct bio_vec *bvec;

	bio = bio_struct->bio;
	pr_info("bio idx:%u.\n", bio->bi_idx);
	/**
	 * FIXME
	 */
	bio_for_each_segment(bvec, bio, i) {
		__free_page(bvec->bv_page);
	}
	bio_put(bio);
	kfree(bio_struct);
}

void dump_bio_wrapper(struct bio_wrapper *bio_wrapper)
{
	struct bio_struct *bio_struct;

	pr_info("--------dump_bio_wrapper start:-----------\n");
	pr_info("wrapper bio:%p\n", bio_wrapper->bio);
	list_for_each_entry(bio_struct, &bio_wrapper->bio_list, list) {
		pr_info("bio_struct:%p, bio:%p, sector:%lu.\n",
				bio_struct, bio_struct->bio, bio_struct->sector);
	}
	pr_info("--------dump_bio_wrapper end:-----------\n");
}

struct meta *init_meta(struct bio *bio)
{
	struct meta *meta;

	meta = kzalloc(sizeof(struct meta), GFP_KERNEL);
	if (meta == NULL)
		return NULL;

	meta->disk_sector = bio->bi_sector;
	return meta;
}

int bio_add_meta_page(struct bio *bio)
{
	struct meta *meta;
	struct page *page;

	page = alloc_page(GFP_KERNEL);
	if(page == NULL) {
		return -1;
	}

	meta = init_meta(bio);
	if (meta == NULL) {
		goto err;
	}
	memcpy(page_address(page), meta, sizeof(struct meta));
	if (bio_add_page(bio, page, sizeof(struct meta), 0) == 0) {
		goto err;
	}

	pr_info("add meta success, meta->sector:%lu.\n",
			meta->disk_sector);
	//pr_content(meta, 512);
	return 0;

err:
	__free_page(page);
	return -1;
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
}

void srl_head_inc(struct srl *srl)
{
	atomic64_add(9, &srl->head);
}

void bio_free_pages(struct bio *bio)
{
	int i;
	struct bio_vec *bvec;

	bio_for_each_segment(bvec, bio, i) {
		__free_page(bvec->bv_page);
	}
}

int bio_add_srl_data(struct bio *bio, int pages)
{
	int idx;
	int size;
	struct page *page;

	for (idx = 0; idx < pages; idx++) {
		page = alloc_page(GFP_KERNEL);
		if (page == NULL) {
			goto fail;
		}

		size = idx ? PAGE_SIZE : META_SIZE;
		if (bio_add_page(bio, page, size, 0) != size) {
			__free_page(page);
			goto fail;
		}
	}

	return 0;

fail:
	bio_free_pages(bio);
	return -1;
}
