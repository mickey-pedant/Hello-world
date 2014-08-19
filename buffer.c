#include <linux/slab.h>

#include "buffer.h"

void dump_buffer_inuse(struct data_buffer *buffer)
{
	struct srl_data *srl_data;

	pr_info("dump inuse buffer:\n");
	list_for_each_entry(srl_data, &buffer->inuse_list, list_inuse) {
		pr_info("-> %lu ", srl_data->disk_sector);
	}
	pr_info("\n");
}

void dump_buffer_data(struct data_buffer *buffer)
{
	struct srl_data *srl_data;

	pr_info("dump data buffer:\n");
	list_for_each_entry(srl_data, &buffer->data_list, list) {
		pr_info("-> %lu ", srl_data->disk_sector);
	}
	pr_info("\n");
}

struct srl_data *init_srl_data(sector_t srl_sector, sector_t disk_sector,
		struct page *data_page)
{
	struct srl_data *srl_data;

	srl_data = kmalloc(sizeof(struct srl_data), GFP_KERNEL);
	if (srl_data == NULL) {
		return NULL;
	}

	INIT_LIST_HEAD(&srl_data->list);
	INIT_LIST_HEAD(&srl_data->list_inuse);
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

	INIT_LIST_HEAD(&buffer->inuse_list);
	INIT_LIST_HEAD(&buffer->data_list);
	spin_lock_init(&buffer->lock);
	init_completion(&buffer->compl);
	buffer->data_size = 0;
	buffer->inuse_size = 0;
	buffer->maxsize = maxsize;

	return buffer;
}

void free_data_buffer(struct data_buffer *buffer)
{
	struct srl_data *srl_data;
	struct srl_data *tmp_data;

	spin_lock(&buffer->lock);
	list_for_each_entry_safe(srl_data, tmp_data, &buffer->data_list, list) {
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

	pr_info("begin search for the buffer.search sector:%lu\n", disk_sector);
	spin_lock(&buffer->lock);
	dump_buffer_inuse(buffer);
	/* manual construct the search list */
	//buffer->data_list.prev->next = &buffer->head;
	list_for_each_entry_reverse(data_iter, &buffer->inuse_list, list_inuse) {
		if (data_iter->disk_sector == disk_sector) {
			srl_data = data_iter;
			break;
		}
	}
	//buffer->head.prev->next = &buffer->data_list;
	spin_unlock(&buffer->lock);

	return srl_data;
}

static void __buffer_trunc(struct data_buffer *buffer)
{
	struct srl_data *head_data;
	struct srl_data *data_iter;
	struct srl_data *tmp_data;

	spin_lock(&buffer->lock);
	head_data = list_first_entry(&buffer->inuse_list, struct srl_data, list_inuse);
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
		pr_info("buffer is full\n");
		__buffer_trunc(buffer);
	}

	if (buffer_is_full(buffer)) {
		return -1;
	}

	spin_lock(&buffer->lock);
	list_add_tail(&srl_data->list, &buffer->data_list);
	list_add_tail(&srl_data->list_inuse, &buffer->inuse_list);
	pr_info("bufer add sector:%lu.\n", srl_data->disk_sector);
	//dump_buffer_data(buffer);
	//dump_buffer_inuse(buffer);
	buffer->data_size++;
	buffer->inuse_size++;
	spin_unlock(&buffer->lock);

	return 0;
}

void buffer_inuse_del(struct data_buffer *buffer)
{

	spin_lock(&buffer->lock);
	//dump_buffer_inuse(buffer);
	if (!list_empty(&buffer->inuse_list)) {
		list_del(buffer->inuse_list.next);
		buffer->inuse_size--;
		if (buffer->inuse_size == buffer->maxsize - 1) {
			complete(&buffer->compl);
		}
	}
	//dump_buffer_inuse(buffer);
	spin_unlock(&buffer->lock);
}
