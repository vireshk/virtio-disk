#include "kvm/util.h"
#include "kvm/kvm.h"
#include "kvm/virtio.h"

#include <sys/eventfd.h>
#include <linux/virtio_ring.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/types.h>
#include <pthread.h>

#include "../demu.h"

#define VIRTIO_I2C_F_ZERO_LENGTH_REQUEST 0;

#define PCI_CLASS_COMMUNICATION_OTHER    0x0780

#define VIRTIO_I2C_QUEUE_SIZE		1024
#define NUM_VIRT_QUEUES			1

static struct i2c_dev *gi2cdev;
struct i2c_dev {
	struct list_head		list;
	struct virtio_device		vdev;
	struct i2c_params		*i2c_param;
	u32				features;
	struct virt_queue		vqs[NUM_VIRT_QUEUES];
	int				call;
	volatile int			kick;
	pthread_t			call_thread;
	struct kvm			*kvm;
};

extern volatile struct i2c_params i2c_params[MAX_I2C];
static LIST_HEAD(i2cdevs);

static u32 get_host_features(struct kvm *kvm, void *dev)
{
	// this matches the current libvhost defaults except VHOST_F_LOG_ALL
	return 1UL << VIRTIO_F_NOTIFY_ON_EMPTY
		| 1UL << VIRTIO_RING_F_INDIRECT_DESC
		| 1UL << VIRTIO_RING_F_EVENT_IDX
		| 1UL << VIRTIO_I2C_F_ZERO_LENGTH_REQUEST;
}

static void set_guest_features(struct kvm *kvm, void *dev, u32 features)
{
}

static void notify_status(struct kvm *kvm, void *dev, u32 status)
{
}

#define NR_GUEST_RAM 2
extern uint64_t guest_ram_base[NR_GUEST_RAM];

static void *i2c_call_thread(void *dev)
{
	struct i2c_dev *i2cdev = dev;
	u64 data;
	int r;

	kvm__set_thread_name("virtio-i2c-call");

	while (true) {
		r = read(i2cdev->call, &data, sizeof(u64));
		if (r < 0)
			continue;
		xen_wmb();
		i2cdev->vdev.ops->signal_vq(i2cdev->kvm, &i2cdev->vdev, 0);
	}

	pthread_exit(NULL);
	return NULL;
}

static int init_vq(struct kvm *kvm, void *dev, u32 vq, u32 page_size, u32 align,
		   u32 pfn)
{
	uint64_t offset = (u64)pfn * page_size;
	struct i2c_dev *i2cdev = dev;
	struct virt_queue *queue;
	void *p;

	BUG_ON(align != PAGE_SIZE);
	BUG_ON(page_size != PAGE_SIZE);

	queue		= &i2cdev->vqs[vq];
	queue->pfn	= pfn;

#if 0
	p		= virtio_get_vq(kvm, queue->pfn, page_size);
#endif
#ifndef MAP_IN_ADVANCE
	p = demu_map_guest_range(offset,
			vring_size(VIRTIO_I2C_QUEUE_SIZE, align));
#else
	p = demu_get_host_addr(offset);

	vring_init(&queue->vring, VIRTIO_I2C_QUEUE_SIZE, (void *)(offset), align);

	i2c_params[0].mem = guest_ram_base[0];
	i2c_params[0].desc = (unsigned long long) queue->vring.desc;
	i2c_params[0].used = (unsigned long long) queue->vring.used;
	i2c_params[0].avail = (unsigned long long) queue->vring.avail;
#endif

	vring_init(&queue->vring, VIRTIO_I2C_QUEUE_SIZE, p, align);
	virtio_init_device_vq(&i2cdev->vdev, queue);

//	if (pthread_create(&i2cdev->call_thread, NULL, i2c_call_thread, i2cdev))
//		return -errno;

	gi2cdev = i2cdev;
	return 0;
}

void call_guest(void)
{
	gi2cdev->vdev.ops->signal_vq(gi2cdev->kvm, &gi2cdev->vdev, 0);
}

static int notify_vq(struct kvm *kvm, void *dev, u32 vq)
{
	struct i2c_dev *i2cdev = dev;
	u64 data = 1;
	int r;

	r = write(i2cdev->kick, &data, sizeof(data));
	if (r < 0)
		return r;
	xen_wmb();

	return 0;
}

static void exit_vq(struct kvm *kvm, void *dev, u32 vq)
{
	struct i2c_dev *i2cdev = dev;
#ifndef MAP_IN_ADVANCE
	struct virt_queue *queue;
#endif

	if (vq != 0)
		return;

	notify_vq(kvm, dev, vq);
	pthread_join(i2cdev->call_thread, NULL);

#ifndef MAP_IN_ADVANCE
	queue = &i2cdev->vqs[vq];
	demu_unmap_guest_range(queue->vring.desc,
			vring_size(VIRTIO_I2C_QUEUE_SIZE, PAGE_SIZE));
#endif
}

static struct virt_queue *get_vq(struct kvm *kvm, void *dev, u32 vq)
{
	struct i2c_dev *i2cdev = dev;

	return &i2cdev->vqs[vq];
}

static int get_size_vq(struct kvm *kvm, void *dev, u32 vq)
{
	/* FIXME: dynamic */
	return VIRTIO_I2C_QUEUE_SIZE;
}

static int set_size_vq(struct kvm *kvm, void *dev, u32 vq, int size)
{
	/* FIXME: dynamic */
	return size;
}

static int get_vq_count(struct kvm *kvm, void *dev)
{
	return NUM_VIRT_QUEUES;
}

static struct virtio_ops i2c_dev_virtio_ops = {
	.get_host_features	= get_host_features,
	.set_guest_features	= set_guest_features,
	.get_vq_count		= get_vq_count,
	.init_vq		= init_vq,
	.exit_vq		= exit_vq,
	.notify_status		= notify_status,
	.notify_vq		= notify_vq,
	.get_vq			= get_vq,
	.get_size_vq		= get_size_vq,
	.set_size_vq		= set_size_vq,
};

static int virtio_i2c__init_one(struct kvm *kvm, struct i2c_params *i2c_param, int index)
{
	struct i2c_dev *i2cdev;
	int r;

	i2cdev = calloc(1, sizeof(struct i2c_dev));
	if (i2cdev == NULL)
		return -ENOMEM;

	*i2cdev = (struct i2c_dev) {
		.i2c_param		= i2c_param,
		.kvm			= kvm,
		.call			= i2c_param->call,
		.kick			= i2c_param->kick,
	};

	list_add_tail(&i2cdev->list, &i2cdevs);

	r = virtio_init(kvm, i2cdev, &i2cdev->vdev, &i2c_dev_virtio_ops,
			VIRTIO_MMIO, 0, 34, PCI_CLASS_COMMUNICATION_OTHER,
			i2c_param->addr, i2c_param->irq);
	if (r < 0)
		return r;

	return 0;
}

static int virtio_i2c__exit_one(struct kvm *kvm, struct i2c_dev *i2cdev)
{
	list_del(&i2cdev->list);
	i2cdev->vdev.ops->exit(kvm, &i2cdev->vdev);
	free(i2cdev);

	return 0;
}

int virtio_i2c__exit(struct kvm *kvm)
{
	while (!list_empty(&i2cdevs)) {
		struct i2c_dev *i2cdev;

		i2cdev = list_first_entry(&i2cdevs, struct i2c_dev, list);
		virtio_i2c__exit_one(kvm, i2cdev);
	}

	return 0;
}
virtio_dev_exit(virtio_i2c__exit);

int virtio_i2c__init(struct kvm *kvm)
{
	int i, r = 0;

	for (i = 0; i < kvm->nr; i++) {
		r = virtio_i2c__init_one(kvm, &kvm->cfg.i2c_params[i], i);
		if (r < 0)
			goto cleanup;
	}

	return 0;
cleanup:
	virtio_i2c__exit(kvm);
	return r;
}
