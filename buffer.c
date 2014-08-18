#include <linux/slab.h>

#include "buffer.h"

struct srl_data *init_srl_data(sector_t srl_sector, sector_t disk_sector,
		struct page *data_page)
{
	struct srl_data *srl_data;

	srl_data = kmalloc(sizeof(struct srl_data), GFP_KERNEL);
	if (srl_data == NULL) {
		return NULL;
	}

	INIT_LIST_HEAD(&srl_data->list);
	srl_data->srl_sector = srl_sector;
	srl_data->disk_sector = disk_sector;
	srl_data->data_page = data_page;

	return srl_data;
}

void free_srl_data(struct srl_data *srl_data)
{
	__free_page(srl_data->data_page);
	kfree(srl_data);
}

struct data_buffer *init_data_buffer(uint64_t maxsize)
{
	struct data_buffer *buffer;

	buffer = kmalloc(sizeof(struct data_buffer), GFP_KERNEL);
	if (buffer == NULL) {
		return NULL;
	}

	INIT_LIST_HEAD(&buffer->head);
	INIT_LIST_HEAD(&buffer->data_list);
	spin_lock_init(&buffer->lock);
	buffer->size = 0;
	buffer->maxsize = maxsize;

	return buffer;
}

void free_data_buffer(struct data_buffer *buffer)
{
	struct srl_data *srl_data;
	struct srl_data *tmp_data;

	spin_lock(&buffer->lock);
	list_for_each_entry_safe(srl_data, tmp_data, &buffer->data_list, list) {
		buffer->size--;
		list_del(&srl_data->list);
		free_srl_data(srl_data);
	}
	spin_unlock(&buffer->lock);

	kfree(buffer);
}

int buffer_is_full(struct data_buffer *buffer)
{
	int ret;

	spin_lock(&buffer->lock);
	ret = buffer->size == buffer->maxsize;
	spin_unlock(&buffer->lock);

	return ret;
}

struct srl_data *get_find_data(struct data_buffer *buffer, sector_t disk_sector)
{
	struct srl_data *data_iter;
	struct srl_data *srl_data = NULL;

	spin_lock(&buffer->lock);
	/* manual construct the search list */
	buffer->data_list.prev->next = &buffer->head;
	list_for_each_entry(data_iter, &buffer->head, list) {
		if (data_iter->disk_sector == disk_sector) {
			srl_data = data_iter;
			break;
		}
	}
	buffer->head.prev->next = &buffer->data_list;
	spin_unlock(&buffer->lock);

	return srl_data;
}

static void __buffer_trunc(struct data_buffer *buffer)
{
	struct srl_data *head_data;
	struct srl_data *data_iter;
	struct srl_data *tmp_data;

	spin_lock(&buffer->lock);
	head_data = list_entry(&buffer->head, struct srl_data, list);
	list_for_each_entry_safe(data_iter, tmp_data, &buffer->data_list,
			list) {
		if (data_iter == head_data) {
			break;
		}
		buffer->size--;
		list_del(&data_iter->list);
		free_srl_data(data_iter);
	}
	spin_unlock(&buffer->lock);
}

int buffer_data_add(struct data_buffer *buffer, struct srl_data *srl_data)
{
	if (buffer_is_full(buffer)) {
		__buffer_trunc(buffer);
	}

	if (buffer_is_full(buffer)) {
		return -1;
	}

	spin_lock(&buffer->lock);
	list_add_tail(&buffer->data_list, &srl_data->list);
	buffer->size++;
	spin_unlock(&buffer->lock);

	return 0;
}
