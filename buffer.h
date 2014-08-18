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
};

struct srl_data *init_srl_data(sector_t srl_sector, sector_t disk_sector,
		struct page *data_page);
void free_srl_data(struct srl_data *srl_data);

struct buffer {
	struct list_head data_list;
	/* data_head -> local_head data */
	struct list_head head;
	spinlock_t lock;
	uint64_t maxsize;
	uint64_t size;
};

struct buffer *init_buffer(uint64_t maxsize);
void free_buffer(struct buffer *buffer);
int buffer_is_full(struct buffer *buffer);

struct srl_data *get_find_data(struct buffer *buffer, sector_t disk_sector);
int buffer_data_add(struct buffer *buffer, struct srl_data *srl_data);

#endif          /* __BIO_BUFFER */
