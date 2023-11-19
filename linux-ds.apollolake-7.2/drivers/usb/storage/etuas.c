/*
 * USB Attached SCSI
 * Note that this is not the same as the USB Mass Storage driver
 *
 * Copyright Matthew Wilcox for Intel Corp, 2010
 * Copyright Sarah Sharp for Intel Corp, 2010
 *
 * Distributed under the terms of the GNU GPL, version two.
 */

#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/usb.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,34))
#include <linux/usb/hcd.h>
#else
#include "../core/hcd.h"
#endif
#include <linux/usb_usual.h>

#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_eh.h>

#include "etuas.h"


#define DRIVER_AUTHOR		"Matthew Wilcox and Sarah Sharp"
#define DRIVER_DESCRIPTION	"USB Attached SCSI Driver"

MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");


/* Overrides scsi_pointer */
struct uas_cmd_info {
	struct list_head list;

	unsigned int state;
	unsigned int stream_id;

	struct urb *data_urb;
	struct urb *siu_urb;
};

enum {
	SUBMIT_SIU_URB		= (1 << 0),
	SUBMIT_DATA_URB		= (1 << 1),
	SUBMIT_CIU_URB		= (1 << 2),
	COMMAND_INFLIGHT	= (1 << 3),
	DATA_INFLIGHT		= (1 << 4),
	COMMAND_ERROR		= (1 << 5),
};

struct uas_dev_info {
	struct usb_interface *intf;
	struct usb_device *udev;
	struct Scsi_Host *shost;

	unsigned cmd_pipe;
	unsigned data_in_pipe;
	unsigned data_out_pipe;
	unsigned status_pipe;

	struct usb_anchor cmd_urbs;
	struct usb_anchor sense_urbs;
	struct usb_anchor data_urbs;
	wait_queue_head_t wait;
	struct list_head busy_list;

	spinlock_t lock;
	unsigned long stream_id_bitmap;
	int total_stream_ids;
	int available_stream_ids;
	int next_available_stream_id;
	struct scsi_cmnd *untagged;

	unsigned long flags;
	unsigned long quirks;
};

#define UAS_FLAG_QUIESCING	0
#define UAS_FLAG_DISCONNECTING	1
#define UAS_FLAG_RESETTING	2

#define UAS_QUIRK_SENSE_IU_R01			(1 << 0)
#define UAS_QUIRK_SENSE_IU_R02			(1 << 1)
#define UAS_QUIRK_SENSE_IU_2R00			(1 << 2)
#define UAS_QUIRK_NO_ATA_PASS_THROUGH	(1 << 3)
#define UAS_QUIRK_NO_TEST_UNIT_READY	(1 << 4)
#define UAS_QUIRK_ONE_STREAM_ID			(1 << 5)
#define UAS_QUIRK_SERIAL_STREAM_ID		(1 << 6)
#define UAS_QUIRK_NO_REPORT_OPCODES		(1 << 7)
#define UAS_QUIRK_BROKEN_FUA			(1 << 8)
#define UAS_QUIRK_NO_REPORT_LUNS		(1 << 9)
#define UAS_QUIRK_INCOMPATIBLE_DEVICE	(1 << 31)

#define IS_ONE_COMMAND_ONLY(quirk)	((quirk) & (UAS_QUIRK_ONE_STREAM_ID | UAS_QUIRK_SERIAL_STREAM_ID))


#define UAS_SUBCLASS_SCSI	0x06
#define UAS_PROTOCOL_BULK	0x50
#define UAS_PROTOCOL_UAS	0x62

#define UAS_MAX_AVAILABLE_STREAMS	16
#define UAS_INVALID_STREAM_ID		0
#define CIU_TAG_UNTAGGED	1
#define CIU_TAG_OFFSET		2

#define UAS_STATE_PROBE			0
#define UAS_STATE_DISCONNECT	1
#define UAS_STATE_PREV_RESET	2
#define UAS_STATE_POST_RESET	3

#define UAS_MAX_COMMANDS	UAS_MAX_AVAILABLE_STREAMS


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
extern int usb_alloc_streams(struct usb_interface *interface,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		unsigned int num_streams, gfp_t mem_flags);
extern void usb_free_streams(struct usb_interface *interface,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		gfp_t mem_flags);
#endif
extern void usb_run_bot_mode_notification(struct usb_device *hdev,
		unsigned int portnum);


static void initialize_stream_id(struct uas_dev_info *devinfo)
{
	devinfo->stream_id_bitmap = 0;
	devinfo->available_stream_ids = devinfo->total_stream_ids;
	devinfo->next_available_stream_id = 1;
}

static int acquire_stream_id(struct uas_dev_info *devinfo)
{
	int stream_id = UAS_INVALID_STREAM_ID;

	if (--devinfo->available_stream_ids >= 0) {
		int retry = devinfo->total_stream_ids;

		while (retry-- > 0) {
			if (!test_bit(devinfo->next_available_stream_id, &devinfo->stream_id_bitmap)) {
				__set_bit(devinfo->next_available_stream_id, &devinfo->stream_id_bitmap);
				stream_id = devinfo->next_available_stream_id;
			}

			if (++devinfo->next_available_stream_id > devinfo->total_stream_ids) {
				devinfo->next_available_stream_id = 1;
			}

			if (UAS_INVALID_STREAM_ID != stream_id) {
				break;
			}
		}

		if (UAS_INVALID_STREAM_ID == stream_id) {
			devinfo->available_stream_ids++;
		}
	}
	else {
		devinfo->available_stream_ids++;
	}

	return stream_id;
}

static void release_stream_id(struct uas_dev_info *devinfo, int stream_id)
{
	if (stream_id <= devinfo->total_stream_ids) {
		__clear_bit(stream_id, &devinfo->stream_id_bitmap);
		devinfo->available_stream_ids++;
	}
}

static int configure_endpoints(struct uas_dev_info *devinfo)
{
	struct usb_interface *intf = devinfo->intf;
	struct usb_device *udev = devinfo->udev;
	struct usb_host_endpoint *eps[4] = {0};
	unsigned char *extra;
	int i, len, max_streams, ret;

	if (udev->state != USB_STATE_CONFIGURED) {
		dev_err(&udev->dev, "%s: Failed to configure endpoints\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
		struct usb_host_ss_ep_comp *comp = intf->cur_altsetting->endpoint[i].ss_ep_comp;

		if (!comp)
			continue;

		extra = comp->extra;
		len = comp->extralen;
#else
		extra = intf->cur_altsetting->endpoint[i].extra;
		len = intf->cur_altsetting->endpoint[i].extralen;
#endif
		while (len >= 3) {
			if (extra[1] == USB_DT_PIPE_USAGE) {
				unsigned pipe_id = extra[2];

				if (pipe_id > 0 && pipe_id < 5) {
					eps[pipe_id - 1] = &intf->cur_altsetting->endpoint[i];
				}
				break;
			}
			len -= extra[0];
			extra += extra[0];
		}
	}

	if (NULL != eps[0]) {
		devinfo->cmd_pipe		= usb_sndbulkpipe(udev, usb_endpoint_num(&eps[0]->desc));
		devinfo->status_pipe	= usb_rcvbulkpipe(udev, usb_endpoint_num(&eps[1]->desc));
		devinfo->data_in_pipe	= usb_rcvbulkpipe(udev, usb_endpoint_num(&eps[2]->desc));
		devinfo->data_out_pipe	= usb_sndbulkpipe(udev, usb_endpoint_num(&eps[3]->desc));

		max_streams = (!(devinfo->quirks & UAS_QUIRK_ONE_STREAM_ID)) ? UAS_MAX_AVAILABLE_STREAMS : 2;
		ret = usb_alloc_streams(devinfo->intf, eps + 1, 3, max_streams, GFP_NOIO);
		if (ret < 0) {
			dev_err(&udev->dev, "%s: Failed to allocate streams (%d)\n", __func__, ret);
		}
		else {
			dev_info(&udev->dev, "%s: Streams allocated = %d\n", __func__, ret);
			devinfo->total_stream_ids = ret;
			if (!(devinfo->quirks & UAS_QUIRK_ONE_STREAM_ID)) {
				initialize_stream_id(devinfo);
			}
		}
	}
	else {
		dev_err(&udev->dev, "%s: Failed to configure endpoints\n", __func__);
		ret = -ENODEV;
	}

	return ret;
}

static void deconfigure_endpoints(struct uas_dev_info *devinfo)
{
	struct usb_device *udev = devinfo->udev;
	struct usb_host_endpoint *eps[3];

	eps[0] = usb_pipe_endpoint(udev, devinfo->status_pipe);
	eps[1] = usb_pipe_endpoint(udev, devinfo->data_in_pipe);
	eps[2] = usb_pipe_endpoint(udev, devinfo->data_out_pipe);
	usb_free_streams(devinfo->intf, eps, 3, GFP_NOIO);
}

static void adjust_device_quirks(struct uas_dev_info *devinfo, const struct usb_device_id *id)
{
	unsigned long masks = (
		UAS_QUIRK_SENSE_IU_R01 |
		UAS_QUIRK_SENSE_IU_R02 |
		UAS_QUIRK_SENSE_IU_2R00 |
		UAS_QUIRK_NO_ATA_PASS_THROUGH |
		UAS_QUIRK_NO_TEST_UNIT_READY |
		UAS_QUIRK_ONE_STREAM_ID |
		UAS_QUIRK_SERIAL_STREAM_ID |
		UAS_QUIRK_NO_REPORT_OPCODES |
		UAS_QUIRK_BROKEN_FUA |
		UAS_QUIRK_NO_REPORT_LUNS
	);

	devinfo->quirks = id->driver_info & masks;
	if (devinfo->quirks) {
		dev_info(&devinfo->udev->dev, "%s: Device quirks = 0x%08lx\n", __func__, devinfo->quirks);
	}
}

static void setup_device_options(struct usb_interface* intf, int type)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);

	if (hcd->driver->update_uas_device) {
		hcd->driver->update_uas_device(hcd, udev, type);
	}
}

static int switch_device_interface(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int i;

	if (!udev->bus->sg_tablesize) {
		struct usb_hcd *hcd = bus_to_hcd(udev->bus);

		dev_warn(&udev->dev, "The driver for the USB controller %s does not "
				"support scatter-gather which is\n",
				hcd->driver->description);
		dev_warn(&udev->dev, "required by the UAS driver. Please try an"
				"alternative USB controller if you wish to use UAS.\n");
		return -ENODEV;
	}

	for (i = 0; i < intf->num_altsetting; i++) {
		struct usb_host_interface *alt = &intf->altsetting[i];

		if (USB_CLASS_MASS_STORAGE == alt->desc.bInterfaceClass &&
			UAS_SUBCLASS_SCSI == alt->desc.bInterfaceSubClass &&
			UAS_PROTOCOL_UAS == alt->desc.bInterfaceProtocol) {
			return usb_set_interface(udev,
						alt->desc.bInterfaceNumber,
						alt->desc.bAlternateSetting);
		}
	}

	return -ENODEV;
}

static bool is_compatible_device(struct usb_interface* intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);

#define USB_QUIRK_BOT_MODE		0x40000000

	switch (hcd->chip_id) {
	case HCD_CHIP_ID_ETRON_EJ168:
	case HCD_CHIP_ID_ETRON_EJ188:
		break;
	default:
		return false;
	}

	if ((USB_SPEED_SUPER != udev->speed) || (udev->quirks & USB_QUIRK_BOT_MODE)) {
		return false;
	}

	if (id->driver_info & UAS_QUIRK_INCOMPATIBLE_DEVICE) {
		usb_set_device_state(udev, USB_STATE_NOTATTACHED);
		usb_run_bot_mode_notification(udev->parent, udev->portnum);
		return false;
	}

	return true;
}

static struct uas_dev_info *scmnd_to_devinfo(struct scsi_cmnd *cmnd)
{
	return (struct uas_dev_info *)cmnd->device->hostdata;
}

static struct uas_cmd_info *scmnd_to_cmdinfo(struct scsi_cmnd *cmnd)
{
	return (void *)&cmnd->SCp;
}

static struct scsi_cmnd *cmdinfo_to_scmnd(struct uas_cmd_info *cmdinfo)
{
	struct scsi_cmnd *cmnd = container_of((struct scsi_pointer *)cmdinfo, struct scsi_cmnd, SCp);

	return cmnd;
}

#ifdef CONFIG_USB_ETRON_UAS_DEBUG
static void show_scsi_command(struct scsi_cmnd *cmnd)
{
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);
	int i;

	printk("cmd:%p (%02X, %d) <", cmnd, cmnd->cmnd[0], cmdinfo->stream_id);
	for (i = 0; i < cmnd->cmd_len; i++) {
		printk(" %02X", cmnd->cmnd[i]);
	}

	printk(" >\n");
}

static void show_sense_data(struct scsi_cmnd *cmnd)
{
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);
	struct scsi_sense_hdr sshdr;

	scsi_normalize_sense(cmnd->sense_buffer, SCSI_SENSE_BUFFERSIZE, &sshdr);
	printk("cmd:%p (%02X, %d) result: %X < code: %X key: %X ASC: %x ASCQ: %X >\n",
		cmnd, cmnd->cmnd[0], cmdinfo->stream_id, cmnd->result,
		sshdr.response_code, sshdr.sense_key, sshdr.asc, sshdr.ascq);
}
#endif /* CONFIG_USB_ETRON_UAS_DEBUG */

static void post_extract_sense_data(struct scsi_cmnd *cmnd)
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	struct usb_device *udev = devinfo->udev;
	u8 sense_key, asc, ascq;

	if ((cmnd->sense_buffer[0] & 0x70) != 0x70) {
		switch (le16_to_cpu(udev->descriptor.idVendor)) {
		case 0x05e3:
			switch (le16_to_cpu(udev->descriptor.idProduct)) {
			case 0x0733:
				if (cmnd->cmnd[0] == TEST_UNIT_READY) {
					sense_key = cmnd->sense_buffer[0];
					asc = cmnd->sense_buffer[1];
					ascq = cmnd->sense_buffer[2];
					memset(cmnd->sense_buffer, 0, 18);
					cmnd->sense_buffer[0] = 0x70;
					cmnd->sense_buffer[2] = sense_key;
					cmnd->sense_buffer[7] = 0x0a;
					cmnd->sense_buffer[12] = asc;
					cmnd->sense_buffer[13] = ascq;
				}
				break;

			default:
				break;
			}
			break;

		default:
			break;
		}
	}
}

static void extract_sense_data(struct urb *urb)
{
	struct scsi_cmnd *cmnd = urb->context;
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	unsigned char *data = urb->transfer_buffer;
	unsigned long masks;
	unsigned len;
	int newlen;

	if (!(data[4] | data[6])) {
		cmnd->result = SAM_STAT_GOOD;
		return;
	}

	masks = UAS_QUIRK_SENSE_IU_R01 | UAS_QUIRK_SENSE_IU_R02 | UAS_QUIRK_SENSE_IU_2R00;
	if (!(devinfo->quirks & masks)) {
		if ((data[8] & 0x70) == 0x70) {
			if (data[5]) {
				devinfo->quirks |= UAS_QUIRK_SENSE_IU_R01;
			}
			else {
				devinfo->quirks |= UAS_QUIRK_SENSE_IU_R02;
			}
		}
		else {
			devinfo->quirks |= UAS_QUIRK_SENSE_IU_2R00;
		}

		dev_info(&devinfo->udev->dev, "%s: Device quirks = 0x%08lx\n", __func__, devinfo->quirks);
	}

	if (devinfo->quirks & UAS_QUIRK_SENSE_IU_R01) {
		struct sense_iu *siu = urb->transfer_buffer;

		len = be16_to_cpu(siu->u.uas1_r01.len) - 2;
		if (len + 8 != urb->actual_length) {
			newlen = min(len + 8, urb->actual_length) - 8;
			if (newlen < 0)
				newlen = 0;
			len = newlen;
		}
		memcpy(cmnd->sense_buffer, siu->u.uas1_r01.sense, len);
		cmnd->result = siu->u.uas1_r01.status;
	}
	else if (devinfo->quirks & UAS_QUIRK_SENSE_IU_R02) {
		struct sense_iu *siu = urb->transfer_buffer;

		len = be16_to_cpu(siu->u.uas1_r02.len);
		if (len + 8 != urb->actual_length) {
			newlen = min(len + 8, urb->actual_length) - 8;
			if (newlen < 0)
				newlen = 0;
			len = newlen;
		}
		memcpy(cmnd->sense_buffer, siu->u.uas1_r02.sense, len);
		cmnd->result = siu->u.uas1_r02.status;
	}
	else {
		struct sense_iu *siu = urb->transfer_buffer;

		len = be16_to_cpu(siu->u.uas2_r00.len);
		if (len + 16 != urb->actual_length) {
			newlen = min(len + 16, urb->actual_length) - 16;
			if (newlen < 0)
				newlen = 0;
			len = newlen;
		}
		memcpy(cmnd->sense_buffer, siu->u.uas2_r00.sense, len);
		cmnd->result = siu->u.uas2_r00.status;
	}

	post_extract_sense_data(cmnd);

#ifdef CONFIG_USB_ETRON_UAS_DEBUG
	show_sense_data(cmnd);
#endif /* CONFIG_USB_ETRON_UAS_DEBUG */
}

static int scsi_command_completion(struct scsi_cmnd *cmnd)
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);

	if (cmdinfo->state & (COMMAND_INFLIGHT | DATA_INFLIGHT)) {
		return -EBUSY;
	}

	if (!(cmdinfo->state & COMMAND_ERROR)) {
		usb_free_urb(cmdinfo->data_urb);
		usb_free_urb(cmdinfo->siu_urb);
	}

	if (!(devinfo->quirks & UAS_QUIRK_ONE_STREAM_ID)) {
		release_stream_id(devinfo, cmdinfo->stream_id);
	}

	cmnd->scsi_done(cmnd);
	return 0;
}

static void transfer_urb_completion(struct urb *urb)
{
	struct scsi_cmnd *cmnd = urb->context;
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
	if (urb->sg) {
		kfree(urb->sg);
	}
#endif

	spin_lock(&devinfo->lock);
	switch (urb->status) {
	case 0:
		if (urb == cmdinfo->siu_urb) {
			struct iu *iu = urb->transfer_buffer;

			if (IU_ID_STATUS == iu->iu_id) {
				extract_sense_data(urb);
			}
			else {
				scmd_printk(KERN_DEBUG, cmnd, "Bogus IU (%d) received\n", iu->iu_id);
				cmnd->result = (DID_OK << 16) | (INITIATOR_ERROR << 8);
			}

			if (cmdinfo->state & DATA_INFLIGHT) {
				scmd_printk(KERN_DEBUG, cmnd, "cmd:%p (%02X, %d) completion without data transfer\n",
						cmnd, cmnd->cmnd[0], cmdinfo->stream_id);
				spin_unlock(&devinfo->lock);
				usb_unlink_urb(cmdinfo->data_urb);
				spin_lock(&devinfo->lock);
			}

			if (devinfo->untagged == cmnd) {
				devinfo->untagged = NULL;
			}

			list_del_init(&cmdinfo->list);
			if (list_empty(&devinfo->busy_list)) {
				wake_up(&devinfo->wait);
			}

			cmdinfo->state &= ~COMMAND_INFLIGHT;
		}
		else {
			scsi_set_resid(cmnd, scsi_bufflen(cmnd) - urb->actual_length);
			cmdinfo->state &= ~DATA_INFLIGHT;
		}
		break;

	case -ECONNRESET:
		if (urb == cmdinfo->data_urb) {
			cmdinfo->state &= ~DATA_INFLIGHT;
		}
		break;

	case -ENOENT:
		if (urb == cmdinfo->siu_urb) {
			list_del_init(&cmdinfo->list);
			if (list_empty(&devinfo->busy_list)) {
				wake_up(&devinfo->wait);
			}

			if (test_bit(UAS_FLAG_DISCONNECTING, &devinfo->flags)) {
				cmdinfo->state &= ~COMMAND_INFLIGHT;
				cmnd->result = DID_NO_CONNECT << 16;
			}
		}
		else {
			if (test_bit(UAS_FLAG_DISCONNECTING, &devinfo->flags)) {
				cmdinfo->state &= ~DATA_INFLIGHT;
			}
		}

		if (test_bit(UAS_FLAG_RESETTING, &devinfo->flags)) {
			usb_free_urb(urb);
			spin_unlock(&devinfo->lock);
			return;
		}
		break;

	default:
		scmd_printk(KERN_DEBUG, cmnd, "Bad URB status (%d) received\n", urb->status);
		if (!test_bit(UAS_FLAG_QUIESCING, &devinfo->flags)) {
			set_bit(UAS_FLAG_QUIESCING, &devinfo->flags);
			list_for_each_entry(cmdinfo, &devinfo->busy_list, list) {
				cmdinfo->state &= ~(DATA_INFLIGHT | COMMAND_INFLIGHT);
				cmdinfo->state |= COMMAND_ERROR;

				cmnd = cmdinfo_to_scmnd(cmdinfo);
				cmnd->result = (DID_OK << 16) | (INITIATOR_ERROR << 8);
				scsi_command_completion(cmnd);
			}
		}
		spin_unlock(&devinfo->lock);
		return;
	}

	scsi_command_completion(cmnd);
	spin_unlock(&devinfo->lock);
}

static int submit_siu_urb(struct scsi_cmnd *cmnd, gfp_t gfp)
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);
	struct urb *siu_urb = NULL;
	struct sense_iu *siu = NULL;
	int ret = 0;

	siu_urb = usb_alloc_urb(0, gfp);
	siu = kzalloc(sizeof(*siu), gfp);
	if (NULL == siu_urb || NULL == siu) {
		ret = -ENOMEM;
		goto free;
	}

	usb_fill_bulk_urb(siu_urb, devinfo->udev, devinfo->status_pipe,
			siu, sizeof(*siu), transfer_urb_completion, cmnd);
	siu_urb->stream_id = cmdinfo->stream_id;
	siu_urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(siu_urb, &devinfo->sense_urbs);
	ret = usb_submit_urb(siu_urb, gfp);
	if (ret < 0) {
		usb_unanchor_urb(siu_urb);
		usb_free_urb(siu_urb);
	}
	else {
		cmdinfo->siu_urb = siu_urb;
	}
	return ret;

free:
	if (siu) {
		kfree(siu);
	}

	if (siu_urb) {
		usb_free_urb(siu_urb);
	}
	return ret;
}

static int submit_data_urb(struct scsi_cmnd *cmnd, gfp_t gfp)
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);
	struct urb *data_urb = NULL;
	bool is_write = (DMA_TO_DEVICE == cmnd->sc_data_direction);
	unsigned int pipe = 0;
	int ret = 0;

	data_urb = usb_alloc_urb(0, gfp);
	if (NULL == data_urb) {
		ret= -ENOMEM;
		goto free;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
	data_urb->sg = kzalloc(sizeof(struct usb_sg_request), gfp);
	if (NULL == data_urb->sg) {
		ret= -ENOMEM;
		goto free;
	}

	data_urb->sg->sg	= scsi_sglist(cmnd);
#else
	data_urb->sg		= scsi_sglist(cmnd);
#endif
	data_urb->num_sgs	= scsi_sg_count(cmnd);
	data_urb->stream_id	= cmdinfo->stream_id;
	pipe = (is_write) ? devinfo->data_out_pipe : devinfo->data_in_pipe;
	usb_fill_bulk_urb(data_urb, devinfo->udev, pipe,
			NULL, scsi_bufflen(cmnd), transfer_urb_completion, cmnd);

	usb_anchor_urb(data_urb, &devinfo->data_urbs);
	ret = usb_submit_urb(data_urb, gfp);
	if (ret < 0) {
		usb_unanchor_urb(data_urb);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
		kfree(data_urb->sg);
#endif
		goto free;
	}
	else {
		cmdinfo->data_urb = data_urb;
	}
	return ret;

free:
	if (data_urb) {
		usb_free_urb(data_urb);
	}

	return ret;
}

static int submit_ciu_urb(struct scsi_cmnd *cmnd, gfp_t gfp)
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);
	struct urb *ciu_urb = NULL;
	struct command_iu *ciu = NULL;
	int ret = 0, len = 0;

	len = cmnd->cmd_len - 16;
	len = (len < 0) ? 0 : len;
	len = ALIGN(len, 4);
	ciu = kzalloc(sizeof(*ciu) + len, gfp);
	ciu_urb = usb_alloc_urb(0, gfp);
	if (NULL == ciu || NULL == ciu_urb) {
		ret = -ENOMEM;
		goto free;
	}

	ciu->iu_id = IU_ID_COMMAND;
	ciu->tag = cpu_to_be16(cmdinfo->stream_id);
	ciu->prio_attr = UAS_SIMPLE_TAG;
	ciu->len = len;
	int_to_scsilun(cmnd->device->lun, &ciu->lun);
	memcpy(ciu->cdb, cmnd->cmnd, cmnd->cmd_len);

	usb_fill_bulk_urb(ciu_urb, devinfo->udev, devinfo->cmd_pipe,
			ciu, sizeof(*ciu) + len, usb_free_urb, NULL);
	ciu_urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(ciu_urb, &devinfo->cmd_urbs);
	ret = usb_submit_urb(ciu_urb, gfp);
	if (ret < 0) {
		usb_unanchor_urb(ciu_urb);
		usb_free_urb(ciu_urb);
	}

#ifdef CONFIG_USB_ETRON_UAS_DEBUG
	show_scsi_command(cmnd);
#endif /* CONFIG_USB_ETRON_UAS_DEBUG */
	return ret;

free:
 	if (ciu) {
		kfree(ciu);
 	}

	if (ciu_urb) {
		usb_free_urb(ciu_urb);
	}

	return ret;
}

static int submit_urbs(struct scsi_cmnd *cmnd, gfp_t gfp)
{
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);
	int ret;

	if (cmdinfo->state & SUBMIT_SIU_URB) {
		ret = submit_siu_urb(cmnd, gfp);
		if (ret < 0) {
			scmd_printk(KERN_DEBUG, cmnd, "SIU URB submission failure\n");
			return ret;
		}

		cmdinfo->state &= ~SUBMIT_SIU_URB;
	}

	if (cmdinfo->state & SUBMIT_DATA_URB) {
		ret = submit_data_urb(cmnd, gfp);
		if (ret < 0) {
			scmd_printk(KERN_DEBUG, cmnd, "Data URB submission failure\n");
			return ret;
		}

		cmdinfo->state &= ~SUBMIT_DATA_URB;
		cmdinfo->state |= DATA_INFLIGHT;
	}

	if (cmdinfo->state & SUBMIT_CIU_URB) {
		ret = submit_ciu_urb(cmnd, gfp);
		if (ret < 0) {
			scmd_printk(KERN_DEBUG, cmnd, "CIU URB submission failure\n");
			return ret;
		}

		cmdinfo->state &= ~SUBMIT_CIU_URB;
		cmdinfo->state |= COMMAND_INFLIGHT;
	}

	return 0;
}

static int generate_stream_id(struct scsi_cmnd *cmnd)
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	int stream_id = CIU_TAG_UNTAGGED;

	devinfo->untagged = cmnd;
	if (!(devinfo->quirks & UAS_QUIRK_ONE_STREAM_ID)) {
		stream_id = acquire_stream_id(devinfo);
		if (!(devinfo->quirks & UAS_QUIRK_SERIAL_STREAM_ID)) {
			devinfo->untagged = NULL;
		}
	}

	return stream_id;
}

/* To Report "Illegal Request: Invalid Field in CDB" */
static unsigned char scsi_sense_invalidCDB[18] = {
	[0]		= 0x70,				/* current error */
	[2]		= ILLEGAL_REQUEST,	/* Illegal Request = 0x05 */
	[7]		= 0x0a,				/* additional length */
	[12]	= 0x24				/* Invalid Field in CDB */
};

static int prev_queue_command(struct scsi_cmnd *cmnd)
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	int ret = -EINVAL;

	if (test_bit(UAS_FLAG_DISCONNECTING, &devinfo->flags)) {
		scmd_printk(KERN_DEBUG, cmnd, "Fail cmd:%p (%02X) during disconnect\n", cmnd, cmnd->cmnd[0]);
		cmnd->result = DID_NO_CONNECT << 16;
		return 0;
	}

	if (test_bit(UAS_FLAG_QUIESCING, &devinfo->flags)) {
		scmd_printk(KERN_DEBUG, cmnd, "Fail cmd:%p (%02X) during transfer error\n", cmnd, cmnd->cmnd[0]);
		cmnd->result = DID_ERROR << 16;
		return 0;
	}

	if (cmnd->sc_data_direction == DMA_BIDIRECTIONAL) {
		scmd_printk(KERN_DEBUG, cmnd, "Fail cmd:%p (%02X) unknown data direction\n", cmnd, cmnd->cmnd[0]);
		cmnd->result = DID_ERROR << 16;
		return 0;
	}

	switch (cmnd->cmnd[0]) {
	case ATA_12:
	case ATA_16:
		if (devinfo->quirks & UAS_QUIRK_NO_ATA_PASS_THROUGH) {
			scmd_printk(KERN_DEBUG, cmnd, "Reject cmd:%p (%02X)\n", cmnd, cmnd->cmnd[0]);
			cmnd->result = SAM_STAT_CHECK_CONDITION;
			memcpy(cmnd->sense_buffer, scsi_sense_invalidCDB, sizeof(scsi_sense_invalidCDB));
			ret = 0;
		}
		break;
	case TEST_UNIT_READY:
		if (devinfo->quirks & UAS_QUIRK_NO_TEST_UNIT_READY) {
			scmd_printk(KERN_DEBUG, cmnd, "Ignore cmd:%p (%02X)\n", cmnd, cmnd->cmnd[0]);
			cmnd->result = SAM_STAT_GOOD;
			ret = 0;
		}
		break;
	default:
		break;
	}

	return ret;
}

static void log_scsi_command_state(struct scsi_cmnd *cmnd)
{
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);

	scmd_printk(KERN_INFO, cmnd, "Log tag:%d, inflight:%s%s%s%s%s%s ",
			cmdinfo->stream_id,
			(cmdinfo->state & SUBMIT_SIU_URB)		? " S-SIU"	: "",
			(cmdinfo->state & SUBMIT_DATA_URB)		? " S-DATA"	: "",
			(cmdinfo->state & SUBMIT_CIU_URB)		? " S-CIU"	: "",
			(cmdinfo->state & DATA_INFLIGHT)		? " DATA"	: "",
			(cmdinfo->state & COMMAND_INFLIGHT) 	? " CMD"	: "",
			(cmdinfo->state & COMMAND_ERROR)	 	? " ERROR"	: "");
	scsi_print_command(cmnd);
}


static int scsi_target_alloc(struct scsi_target *starget)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,0))
	struct uas_dev_info *devinfo = (struct uas_dev_info *)
			dev_to_shost(starget->dev.parent)->hostdata;

	if (devinfo->flags & UAS_QUIRK_NO_REPORT_LUNS)
		starget->no_report_luns = 1;
#endif

	return 0;
}

static int scsi_slave_alloc(struct scsi_device *sdev)
{
	sdev->hostdata = (void *)sdev->host->hostdata;

	/* USB has unusual DMA-alignment requirements: Although the
	 * starting address of each scatter-gather element doesn't matter,
	 * the length of each element except the last must be divisible
	 * by the Bulk maxpacket value.  There's currently no way to
	 * express this by block-layer constraints, so we'll cop out
	 * and simply require addresses to be aligned at 512-byte
	 * boundaries.	This is okay since most block I/O involves
	 * hardware sectors that are multiples of 512 bytes in length,
	 * and since host controllers up through USB 2.0 have maxpacket
	 * values no larger than 512.
	 *
	 * But it doesn't suffice for Wireless USB, where Bulk maxpacket
	 * values can be as large as 2048.	To make that work properly
	 * will require changes to the block layer.
	 */
	blk_queue_update_dma_alignment(sdev->request_queue, (512 - 1));

	return 0;
}

static int scsi_slave_configure(struct scsi_device *sdev)
{
	struct uas_dev_info *devinfo = sdev->hostdata;
	int tags = 1;

	if (!IS_ONE_COMMAND_ONLY(devinfo->quirks))
		tags = devinfo->total_stream_ids;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0))
	scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, tags);
#else
	scsi_change_queue_depth(sdev, tags);
#endif

	blk_queue_rq_timeout(sdev->request_queue, 5 * HZ);

	/* Many disks only accept MODE SENSE transfer lengths of
	 * 192 bytes (that's what Windows uses). */
	sdev->use_192_bytes_for_3f = 1;

	/* A number of devices have problems with MODE SENSE for
	 * page x08, so we will skip it. */
	sdev->skip_ms_page_8 = 1;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,0))
	/* Some devices don't handle VPD pages correctly */
	sdev->skip_vpd_pages = 1;

	/*
	 * Many devices do not respond properly to READ_CAPACITY_16.
	 * Tell the SCSI layer to try READ_CAPACITY_10 first.
	 */
	sdev->try_rc_10_first = 1;
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,6,5))
	/* Do not attempt to use REPORT SUPPORTED OPERATION CODES */
	if (devinfo->flags & UAS_QUIRK_NO_REPORT_OPCODES)
		sdev->no_report_opcodes = 1;

	/* Do not attempt to use WRITE SAME */
	sdev->no_write_same = 1;
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4,4,0))
	/* A few buggy USB-ATA bridges don't understand FUA */
	if (devinfo->flags & UAS_QUIRK_BROKEN_FUA)
		sdev->broken_fua = 1;
#endif

	return 0;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,36))
static int scsi_queue_command_lck(struct scsi_cmnd *cmnd,
					void (*done)(struct scsi_cmnd *))
#else
static int scsi_queue_command(struct scsi_cmnd *cmnd,
					void (*done)(struct scsi_cmnd *))
#endif
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	struct uas_cmd_info *cmdinfo = scmnd_to_cmdinfo(cmnd);
	unsigned long flags;

	BUILD_BUG_ON(sizeof(struct uas_cmd_info) > sizeof(struct scsi_pointer));

	/* Re-check scsi_block_requests now that we've the host-lock */
	if (cmnd->device->host->host_self_blocked)
		return SCSI_MLQUEUE_DEVICE_BUSY;

	if (!prev_queue_command(cmnd)) {
		goto command_done;
	}

	spin_lock_irqsave(&devinfo->lock, flags);
	if (devinfo->untagged) {
		spin_unlock_irqrestore(&devinfo->lock, flags);
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	memset(cmdinfo, 0, sizeof(struct uas_cmd_info));
	cmdinfo->stream_id = generate_stream_id(cmnd);
	if (UAS_INVALID_STREAM_ID == cmdinfo->stream_id) {
		spin_unlock_irqrestore(&devinfo->lock, flags);
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	cmdinfo->state = SUBMIT_CIU_URB | SUBMIT_SIU_URB;
	switch (cmnd->sc_data_direction) {
	case DMA_FROM_DEVICE:
	case DMA_TO_DEVICE:
		cmdinfo->state |= SUBMIT_DATA_URB;
		break;
	default:
		break;
	}

	cmnd->scsi_done = done;
	submit_urbs(cmnd, GFP_ATOMIC);
	INIT_LIST_HEAD(&cmdinfo->list);
	list_add_tail(&cmdinfo->list, &devinfo->busy_list);
	spin_unlock_irqrestore(&devinfo->lock, flags);
	return 0;

command_done:
	done(cmnd);
	return 0;
}
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,36))
static DEF_SCSI_QCMD(scsi_queue_command)
#endif

static int scsi_abort_handler(struct scsi_cmnd *cmnd)
{
	log_scsi_command_state(cmnd);

	return FAILED;
}

static int scsi_reset_bus_handler(struct scsi_cmnd *cmnd)
{
	struct uas_dev_info *devinfo = scmnd_to_devinfo(cmnd);
	int ret = 0;

	if (test_bit(UAS_FLAG_DISCONNECTING, &devinfo->flags)) {
		shost_printk(KERN_INFO, cmnd->device->host, "%s No reset during disconnect\n", __func__);
	}
	else {
		clear_bit(UAS_FLAG_QUIESCING, &devinfo->flags);
		set_bit(UAS_FLAG_RESETTING, &devinfo->flags);
		usb_kill_anchored_urbs(&devinfo->cmd_urbs);
		usb_kill_anchored_urbs(&devinfo->data_urbs);
		usb_kill_anchored_urbs(&devinfo->sense_urbs);
		ret = usb_lock_device_for_reset(devinfo->udev, devinfo->intf);
		if (!ret) {
			ret = usb_reset_device(devinfo->udev);
			usb_unlock_device(devinfo->udev);

			if (!ret) {
				clear_bit(UAS_FLAG_RESETTING, &devinfo->flags);
				shost_printk(KERN_INFO, cmnd->device->host, "%s SUCCESS\n", __func__);
				return SUCCESS;
			}
		}

		clear_bit(UAS_FLAG_RESETTING, &devinfo->flags);
		shost_printk(KERN_INFO, cmnd->device->host, "%s FAILED\n", __func__);
	}

	return FAILED;
}

static struct scsi_host_template uas_host_template = {
	.name		= "etuas",
	.module		= THIS_MODULE,
	.this_id	= -1,

	.sg_tablesize	= SG_NONE,
	.can_queue		= UAS_MAX_COMMANDS,
	.max_sectors	= 128,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0))
	.cmd_per_lun	= 1,		/* until we override it */

	.ordered_tag		= 1,
#endif
	.skip_settle_delay	= 1,

	.target_alloc			= scsi_target_alloc,
	.slave_alloc			= scsi_slave_alloc,
	.slave_configure		= scsi_slave_configure,
	.queuecommand			= scsi_queue_command,
	.eh_abort_handler		= scsi_abort_handler,
	.eh_bus_reset_handler	= scsi_reset_bus_handler,
};


static int probe_device(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct uas_dev_info *devinfo = NULL;
	struct Scsi_Host *shost = NULL;
	int ret = 0;

	if (!is_compatible_device(intf, id) || switch_device_interface(intf)) {
		return -ENODEV;
	}

	dev_info(&udev->dev, "UAS device detected\n");

	shost = scsi_host_alloc(&uas_host_template, sizeof(struct uas_dev_info));
	if (NULL == shost) {
		ret = -ENOMEM;
		goto set_alt0;
	}

	devinfo = (struct uas_dev_info *)shost->hostdata;
	devinfo->intf = intf;
	devinfo->udev = udev;
	devinfo->shost = shost;
	devinfo->untagged = NULL;
	INIT_LIST_HEAD(&devinfo->busy_list);
	spin_lock_init(&devinfo->lock);
	init_usb_anchor(&devinfo->cmd_urbs);
	init_usb_anchor(&devinfo->sense_urbs);
	init_usb_anchor(&devinfo->data_urbs);
	init_waitqueue_head(&devinfo->wait);
	adjust_device_quirks(devinfo, id);
	ret = configure_endpoints(devinfo);
	if (ret < 0) {
		goto set_alt0;
	}

	usb_set_intfdata(intf, devinfo);

	shost->can_queue = 1;
	if (!IS_ONE_COMMAND_ONLY(devinfo->quirks))
		shost->can_queue = devinfo->total_stream_ids;

	shost->max_id = 1;
	shost->max_lun = 256;
	shost->max_channel = 0;
	shost->max_cmd_len = 16 + 252;
	shost->sg_tablesize = udev->bus->sg_tablesize;
	ret = scsi_add_host(shost, &intf->dev);
	if (ret < 0) {
		goto deconfig_eps;
	}

	scsi_scan_host(shost);

	setup_device_options(intf, UAS_STATE_PROBE);
	return ret;

deconfig_eps:
	deconfigure_endpoints(devinfo);
	usb_set_intfdata(intf, NULL);

set_alt0:
	usb_set_interface(udev, intf->altsetting[0].desc.bInterfaceNumber, 0);

	if (shost) {
		scsi_host_put(shost);
	}

	return ret;
}

static void disconnect_device(struct usb_interface *intf)
{
	struct uas_dev_info *devinfo = usb_get_intfdata(intf);

	set_bit(UAS_FLAG_DISCONNECTING, &devinfo->flags);
	usb_kill_anchored_urbs(&devinfo->cmd_urbs);
	usb_kill_anchored_urbs(&devinfo->data_urbs);
	usb_kill_anchored_urbs(&devinfo->sense_urbs);

	scsi_remove_host(devinfo->shost);

	deconfigure_endpoints(devinfo);
	scsi_host_put(devinfo->shost);

	setup_device_options(intf, UAS_STATE_DISCONNECT);
}

static int prev_reset_device(struct usb_interface *intf)
{
	struct uas_dev_info *devinfo = usb_get_intfdata(intf);
	struct Scsi_Host *shost = devinfo->shost;
	unsigned long flags;

	setup_device_options(intf, UAS_STATE_PREV_RESET);

	/* Block new requests */
	spin_lock_irqsave(shost->host_lock, flags);
	scsi_block_requests(shost);
	spin_unlock_irqrestore(shost->host_lock, flags);

	/* Wait for any pending request to complete */
	if (wait_event_timeout(devinfo->wait, list_empty(&devinfo->busy_list),
		msecs_to_jiffies(5000)) == 0) {
		shost_printk(KERN_ERR, shost, "%s Timed out\n", __func__);
		scsi_unblock_requests(shost);
		return 1;
	}

	deconfigure_endpoints(devinfo);

	return 0;
}

static int post_reset_device(struct usb_interface *intf)
{
	struct uas_dev_info *devinfo = usb_get_intfdata(intf);
	struct Scsi_Host *shost = devinfo->shost;
	unsigned long flags;

	setup_device_options(intf, UAS_STATE_POST_RESET);

	devinfo->untagged = NULL;
	INIT_LIST_HEAD(&devinfo->busy_list);
	if (configure_endpoints(devinfo) < 0) {
		return 1;
	}

	spin_lock_irqsave(shost->host_lock, flags);
	scsi_report_bus_reset(shost, 0);
	spin_unlock_irqrestore(shost->host_lock, flags);

	scsi_unblock_requests(shost);

	return 0;
}

static struct usb_device_id uas_usb_ids[] = {
	{ USB_DEVICE_VER(0x0984, 0x0301, 0x0128, 0x0128), .driver_info = UAS_QUIRK_INCOMPATIBLE_DEVICE },
	{ USB_DEVICE_VER(0x4971, 0x1013, 0x4896, 0x4896), .driver_info = UAS_QUIRK_INCOMPATIBLE_DEVICE },
	{ USB_DEVICE_VER(0x4971, 0x1012, 0x4798, 0x4798), .driver_info = UAS_QUIRK_INCOMPATIBLE_DEVICE },
	{ USB_DEVICE_VER(0x059b, 0x0070, 0x0006, 0x0006), .driver_info = UAS_QUIRK_INCOMPATIBLE_DEVICE },
	{ USB_DEVICE_VER(0x1759, 0x5002, 0x2270, 0x2270), .driver_info = UAS_QUIRK_NO_TEST_UNIT_READY },
	{ USB_DEVICE_VER(0x0bc2, 0x2312, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x0bc2, 0x3312, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x0bc2, 0x3320, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x0bc2, 0xa003, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x0bc2, 0xa013, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x0bc2, 0xa0a4, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH | UAS_QUIRK_SERIAL_STREAM_ID },
	{ USB_DEVICE_VER(0x0bc2, 0xab20, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x0bc2, 0xab21, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x0bc2, 0xab2a, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x13fd, 0x3940, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x2109, 0x0711, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_ATA_PASS_THROUGH },
	{ USB_DEVICE_VER(0x152d, 0x0539, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_REPORT_OPCODES },
	{ USB_DEVICE_VER(0x152d, 0x0567, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_REPORT_OPCODES | UAS_QUIRK_BROKEN_FUA },
	{ USB_DEVICE_VER(0x357d, 0x7788, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_REPORT_OPCODES },
	{ USB_DEVICE_VER(0x4971, 0x8017, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_REPORT_OPCODES },
	{ USB_DEVICE_VER(0x0bc2, 0x331a, 0x0000, 0x9999), .driver_info = UAS_QUIRK_NO_REPORT_LUNS },
	{ USB_DEVICE_VER(0x05e3, 0x0733, 0x5405, 0x5405), .driver_info = UAS_QUIRK_ONE_STREAM_ID },
	{ USB_DEVICE_VER(0x174c, 0x5106, 0x0100, 0x0100), .driver_info = UAS_QUIRK_SERIAL_STREAM_ID },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, UAS_SUBCLASS_SCSI, UAS_PROTOCOL_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, UAS_SUBCLASS_SCSI, UAS_PROTOCOL_UAS) },
	{ }
};
MODULE_DEVICE_TABLE(usb, uas_usb_ids);

static struct usb_driver uas_driver = {
	.name		= "etuas",
	.id_table	= uas_usb_ids,

	.probe		= probe_device,
	.disconnect	= disconnect_device,
	.pre_reset	= prev_reset_device,
	.post_reset	= post_reset_device,
};


#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,3,0))
module_usb_driver(uas_driver);
#else
static int etuas_init(void)
{
	return usb_register(&uas_driver);
}

static void etuas_exit(void)
{
	usb_deregister(&uas_driver);
}

module_init(etuas_init);
module_exit(etuas_exit);
#endif

