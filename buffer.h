#ifndef __BIO_BUFFER_H
#define __BIO_BUFFER_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mm_types.h>

struct srl_data {
	sector_t srl_sector;
	sector_t disk_sector;
	struct page *data_page;

	struct list_head list;
	struct list_head list_inuse;
};

struct srl_data *init_srl_data(sector_t srl_sector, sector_t disk_sector,
		struct page *data_page);
void free_srl_data(struct srl_data *srl_data);

struct data_buffer {
	struct list_head data_list;
	/* inuse_list -> local_head data */
	struct list_head inuse_list;
	spinlock_t lock;
	uint64_t maxsize;
	uint64_t size;
};

struct data_buffer *init_data_buffer(uint64_t maxsize);
void free_data_buffer(struct data_buffer *buffer);
int buffer_is_full(struct data_buffer *buffer);

struct srl_data *get_find_data(struct data_buffer *buffer, sector_t disk_sector);
int buffer_data_add(struct data_buffer *buffer, struct srl_data *srl_data);
void buffer_inuse_del(struct data_buffer *buffer);

#endif          /* __BIO_BUFFER */
