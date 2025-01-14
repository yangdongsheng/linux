#include "cbd_internal.h"

static ssize_t cbd_backend_id_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct cbd_channel_device *channel;
	struct cbd_channel_info *channel_info;

	channel = container_of(dev, struct cbd_channel_device, dev);
	channel_info = channel->channel_info;

	if (channel_info->backend_state == cbdc_backend_state_none)
		return 0;

	return sprintf(buf, "%u\n", channel_info->backend_id);
}

static ssize_t cbd_blkdev_id_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct cbd_channel_device *channel;
	struct cbd_channel_info *channel_info;

	channel = container_of(dev, struct cbd_channel_device, dev);
	channel_info = channel->channel_info;

	if (channel_info->blkdev_state == cbdc_blkdev_state_none)
		return 0;

	return sprintf(buf, "%u\n", channel_info->blkdev_id);
}

static DEVICE_ATTR(backend_id, 0400, cbd_backend_id_show, NULL);
static DEVICE_ATTR(blkdev_id, 0400, cbd_blkdev_id_show, NULL);

static struct attribute *cbd_channel_attrs[] = {
	&dev_attr_backend_id.attr,
	&dev_attr_blkdev_id.attr,
	NULL
};

static struct attribute_group cbd_channel_attr_group = {
	.attrs = cbd_channel_attrs,
};

static const struct attribute_group *cbd_channel_attr_groups[] = {
	&cbd_channel_attr_group,
	NULL
};

static void cbd_channel_release(struct device *dev)
{
}

struct device_type cbd_channel_type = {
	.name		= "cbd_channel",
	.groups		= cbd_channel_attr_groups,
	.release	= cbd_channel_release,
};

struct device_type cbd_channels_type = {
	.name		= "cbd_channels",
	.release	= cbd_channel_release,
};

void cbdc_copy_to_bio(struct cbd_channel *channel,
		u32 data_off, u32 data_len, struct bio *bio)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	void *src, *dst;
	u32 data_head = data_off;
	u32 to_copy, page_off = 0;

	cbdt_flush_range(channel->cbdt, channel->data + data_off, data_len);
next:
	bio_for_each_segment(bv, bio, iter) {
		dst = kmap_atomic(bv.bv_page);
		page_off = bv.bv_offset;
again:
		if (data_head >= CBDC_DATA_SIZE) {
			data_head &= CBDC_DATA_MASK;
		}

		src = channel->data + data_head;
		to_copy = min(bv.bv_offset + bv.bv_len - page_off,
			      CBDC_DATA_SIZE - data_head);
		memcpy_flushcache(dst + page_off, src, to_copy);

		/* advance */
		data_head += to_copy;
		page_off += to_copy;

		/* more data in this bv page */
		if (page_off < bv.bv_offset + bv.bv_len) {
			goto again;
		}
		kunmap_atomic(dst);
		flush_dcache_page(bv.bv_page);
	}

	if (bio->bi_next) {
		bio = bio->bi_next;
		goto next;
	}

	return;
}

void cbdc_copy_from_bio(struct cbd_channel *channel,
		u32 data_off, u32 data_len, struct bio *bio)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	void *src, *dst;
	u32 data_head = data_off;
	u32 to_copy, page_off = 0;

next:
	bio_for_each_segment(bv, bio, iter) {
		flush_dcache_page(bv.bv_page);

		src = kmap_atomic(bv.bv_page);
		page_off = bv.bv_offset;
again:
		if (data_head >= CBDC_DATA_SIZE) {
			data_head &= CBDC_DATA_MASK;
		}

		dst = channel->data + data_head;
		to_copy = min(bv.bv_offset + bv.bv_len - page_off,
			      CBDC_DATA_SIZE - data_head);

		memcpy_flushcache(dst, src + page_off, to_copy);

		/* advance */
		data_head += to_copy;
		page_off += to_copy;

		/* more data in this bv page */
		if (page_off < bv.bv_offset + bv.bv_len) {
			goto again;
		}
		kunmap_atomic(src);
	}

	if (bio->bi_next) {
		bio = bio->bi_next;
		goto next;
	}

	cbdt_flush_range(channel->cbdt, channel->data + data_off, data_len);

	return;
}

void cbdc_flush_ctrl(struct cbd_channel *channel)
{
	flush_dcache_page(channel->ctrl_page);
}

void cbd_channel_init(struct cbd_channel *channel, struct cbd_transport *cbdt, u32 channel_id)
{
	struct cbd_channel_info *channel_info = cbdt_get_channel_info(cbdt, channel_id);

	channel->cbdt = cbdt;
	channel->channel_info = channel_info;
	channel->channel_id = channel_id;
	channel->cmdr = (void *)channel_info + CBDC_CMDR_OFF;
	channel->compr = (void *)channel_info + CBDC_COMPR_OFF;
	channel->data = (void *)channel_info + CBDC_DATA_OFF;
	channel->data_size = CBDC_DATA_SIZE;
	channel->ctrl_page = cbdt_page(cbdt, (void *)channel_info - (void *)cbdt->transport_info);

	spin_lock_init(&channel->cmdr_lock);
	spin_lock_init(&channel->compr_lock);
}
