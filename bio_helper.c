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

void hadm_bio_end_io(struct bio *bio, int err)
{
        void *src;
        struct bio_wrapper *bio_w;
        struct bio_struct *iter;
        struct bio_struct *bio_struct = NULL;

	bio_w = bio->bi_private;

	pr_info("handler bio READ begin");
        //if(0) {
        if(bio_data_dir(bio_w->bio) == READ) {
                list_for_each_entry(iter, &bio_w->bio_list, list) {
                        if (iter->bio == bio) {
                                bio_struct = iter;
                        }
                }

                //memset(page_address(bvec->bv_page), 'C', PAGE_SIZE);
                /**
                 * TODO check sector
                 */
                if (bio_struct == NULL) {
                        pr_info("bug, can't find origin bio_struct.\n");
                } else {
                        pr_info("copy bio data\n");
                        pr_c_content(page_address(bio->bi_io_vec[0].bv_page), 512);
                        //memset(page_address(bio_w->bio->bi_io_vec[0].bv_page), 'C', PAGE_SIZE);
                        src = page_address(bio_w->bio->bi_io_vec[bio_struct->idx].bv_page);
                        if (src == NULL) {
                                pr_info("BUG!!!!!!!!!! src is NULL.");
                        } else {
                                memcpy(page_address(bio_w->bio->bi_io_vec[bio_struct->idx].bv_page),
                                                page_address(bio->bi_io_vec[0].bv_page), PAGE_SIZE);
                        }
                        //memcpy(page_address(bvec->bv_page), page_address(bio->bi_io_vec[0].bv_page), PAGE_SIZE);
                }
        }
	pr_info("handler bio READ end");

        if (err)
                bio_w->err |= err;
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

int hadm_bio_split(struct bio_wrapper *wrapper, bio_end_io_t *bi_end_io,
                struct block_device *bdev)
{
        struct list_head *bio_list = &wrapper->bio_list;
	//struct bio_list *bio_list;
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

		bio->bi_sector = bio_src->bi_sector + 8 * i;
		// bio->bi_bdev = bio_src->bi_bdev;
		bio->bi_bdev = bdev;
		bio->bi_flags = bio_src->bi_flags;
		bio->bi_rw = bio_src->bi_rw;
		bio->bi_private = wrapper;
		bio->bi_end_io = bi_end_io;

                if (bio_data_dir(bio) == WRITE) {
                        bio_add_meta_page(bio);
                }

		page = alloc_page(GFP_KERNEL);
		if(page == NULL) {
			bio_put(bio);
			goto err_bio;
		}

                addr = page_address(page);
		addr_src = page_address(bvec->bv_page);
		memcpy(addr, addr_src, PAGE_SIZE);

		if(bio_add_page(bio, page, bvec->bv_len, bvec->bv_offset) == 0) {
			__free_page(page);
			bio_put(bio);
			goto err_bio;
		}

                //dump_bio(bio, __FUNCTION__);
                bio_struct = init_bio_struct(bio, i);
                if (bio_struct == NULL) {
                        goto err_bio;
                }

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
        //void *src_addr;
        //void *dst_addr;

        /* at least one bio in the list */
        list_for_each_entry(bio_struct, bio_list, list) {
                //pr_info("===== submit bio =====");

                bio = bio_struct->bio;
                //pr_info("submit list bio: %p\n", bio);
                // dump_bio(bio, __FUNCTION__);
                schedule();             /* FIXME XXOOXX */
                //msleep(10);
                /*
                   if (bio_data_dir(bio) == READ) {
                   buffer_data = find_get_bio_data(bio, bio_buffer);
                   if (buffer_data != NULL) {
                   src_addr = page_address(buffer_data->data);
                   dst_addr = page_address(bio->bi_io_vec[0].bv_page);
                   memcpy(dst_addr, src_addr, PAGE_SIZE);

                   if (atomic_dec_and_test(&bio_wrapper->count)) {
                   bio_endio(bio_w->bio, 0);
                   }
                   bio = bio->bi_next;
                   continue;
                   } else {

                 * write bdev -> srl
                 * read bdev -> disk


                //bio->bdev = disk->bdev;
                }

                }
                */

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
int bio_add_meta_page(struct bio *bio)
{
        return 0;
}

struct bio_wrapper *init_bio_wrapper(struct bio *bio, bio_end_io_t *end_io, struct block_device *bdev)
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

        ret = hadm_bio_split(wrapper, end_io, bdev);
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

struct bio_struct *init_bio_struct(struct bio* bio, int idx)
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
