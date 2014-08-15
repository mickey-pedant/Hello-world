#ifndef __BIO_HELPER_H__
#define __BIO_HELPER_H__

#define MAX_BIO_WRAPPER_LIST_SIZE (1 << 14)
#define META_SIZE 512
#define BLK_SHIFT 9

#include "miniblk.h"

struct bio_struct {
        struct bio *bio;
        sector_t sector;
        int idx;

        struct list_head list;
};

struct meta {
        union {
                sector_t disk_sector;
                char pack[META_SIZE];
        };
};

struct bio_wrapper {
	struct bio *bio;
        struct list_head bio_list;
	//struct bio_list *bio_list;
	atomic_t count;
        uint32_t err;                   /* err flag */

        bio_end_io_t *end_io;

        struct list_head list;          /* bio_wrapper list */
};

struct bio_wrapper_list {
        struct list_head bio_wrapper;
        uint64_t count;
        uint64_t maxsize;
        struct completion complete;
        spinlock_t lock;
};

struct buffer_data {
        sector_t disk_sector;
        sector_t srl_sector;
        struct page *data;
};

struct bio;

int hadm_bio_split(struct bio_wrapper *wrapper, bio_end_io_t *bi_end_io);
// struct bio_list *hadm_bio_split(struct bio *bio_src);
void hadm_bio_list_free(struct list_head *bio_list);
void hadm_bio_list_dump(struct bio_list *bio_list); /* FIXME */
void submit_bio_list(struct list_head *bio_list);
void free_bio_wrapper(struct bio_wrapper *bio_w);
void hadm_bio_end_io(struct bio *bio, int err);

struct bio_wrapper_list *init_bio_wrapper_list(uint64_t maxsize);
struct bio_wrapper *bio_wrapper_list_get(struct bio_wrapper_list *bio_wrapper_list);
int bio_wrapper_list_put(struct bio_wrapper_list *bio_wrapper_list, struct bio_wrapper *bio_wrapper);
struct bio_wrapper *alloc_bio_wrapper(void);
struct bio_wrapper *init_bio_wrapper(struct bio *bio, bio_end_io_t *end_io);

int bio_wrapper_add_meta(void);
int bio_add_meta_page(struct bio *bio);
//void dump_bio_wrapper(struct bio *bio);

void pr_c_content(void *addr, unsigned int size);
void pr_content(void *addr, unsigned int size);
void dump_bio(struct bio *bio, const char *msg);
void dump_wrapper_list(struct bio_wrapper_list *wrapper_list, const char *msg);
void dump_bio_wrapper(struct bio_wrapper *bio_wrapper);

struct bio_struct *init_bio_struct(struct bio* bio, int idx);
void free_bio_struct(struct bio_struct *bio_struct);

int bio_add_meta_page(struct bio *bio);
sector_t srl_tail(struct srl *srl);
void srl_tail_inc(struct srl *srl);
void bio_free_page(struct bio *bio);
#endif // __BIO_HELPER_H__
