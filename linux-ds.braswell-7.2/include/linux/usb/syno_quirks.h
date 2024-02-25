#ifndef __LINUX_SYNO_USB_QUIRKS_H
#define __LINUX_SYNO_USB_QUIRKS_H

/*
 * Description:
 *   UPS_DISCONNECT_FILTER is used to avoid disconnection from the UPS by
 *   deferring a disconnect for a period of time, and then if the UPS re-
 *   connects, we ignore the last disconnection, otherwise disconnects it.
 *
 *   LIMITED_UPS_DISCONNECT_FILTERING is an extension of UPS_DISCONNECT_FILTER
 *   it's used to limit the number of disconnect filtering in a period of time.
 *   For the buggy UPS which disconnects very frequently before a UPS driver (
 *   usually in userspace) links the UPS. Because the condition that the buggy
 *   UPS isn't stable initally and UPS driver can't also link it before the
 *   driver stops trying, so we should actually disconnect and re-connect to
 *   notify and restart the UPS driver
 *
 *   SYNCHRONIZE_CACHE_FILTER is used to avoid sending SYNCHRONIZE_CACHE, a SCSI
 *   command, which flushes volatile cache of a USB storage to its non-volatile
 *   storage, by replacing the command with msleep().
 *
 *   HC_MORE_TRANSACTION_TRIES is used to retry a bulk transaction if
 *   transaction errors.
 */

#define SYNO_USB_QUIRK_UPS_DISCONNECT_FILTER				0x00000001
#define SYNO_USB_QUIRK_LIMITED_UPS_DISCONNECT_FILTERING		0x00000002
#define SYNO_USB_QUIRK_SYNCHRONIZE_CACHE_FILTER				0x00000010
#define SYNO_USB_QUIRK_HC_MORE_TRANSACTION_TRIES			0x00000020

#endif /* __LINUX_SYNO_USB_QUIRKS_H */

