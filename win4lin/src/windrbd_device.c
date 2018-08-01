/*
	Copyright(C) 2017-2018, Johannes Thoma <johannes@johannesthoma.com>
	Copyright(C) 2017-2018, LINBIT HA-Solutions GmbH  <office@linbit.com>

	Windows DRBD is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	Windows DRBD is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Windows DRBD; see the file COPYING. If not, write to
	the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* This file contains the handler on the windrbd device (matching the
 * /dev/drbd<n> devices in Linux). Requests to a windrbd device (such
 * as called by a CreateFile, WriteFile and the like) are handled first
 * herein and then (if neccessary) forwarded to the corresponding
 * DRBD handlers. For functions related to accessing the DRBD backing
 * devices (the 'physical' devices), see drbd_windows.c
 */

#include <wdm.h>
#include <ntddk.h>
#include <ntdddisk.h>

/* Uncomment this if you want more debug output (disable for releases) */
#define DEBUG 1

#include "drbd_windows.h"
#include "windrbd_device.h"
#include "drbd_int.h"
#include "drbd_wrappers.h"

static NTSTATUS windrbd_not_implemented(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_SUCCESS;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	struct _IO_STACK_LOCATION *s = IoGetCurrentIrpStackLocation(irp);
	
	dbg(KERN_DEBUG "DRBD device request not implemented: MajorFunction: 0x%x\n", s->MajorFunction);
	irp->IoStatus.Status = STATUS_NOT_IMPLEMENTED;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_NOT_IMPLEMENTED;
}

static void fill_drive_geometry(struct _DISK_GEOMETRY *g, struct block_device *dev)
{
	g->BytesPerSector = dev->bd_block_size;
	g->Cylinders.QuadPart = dev->d_size / dev->bd_block_size;
	g->TracksPerCylinder = 1;
	g->SectorsPerTrack = 1;
	g->MediaType = FixedMedia;
}

static void fill_partition_info(struct _PARTITION_INFORMATION *p, struct block_device *dev)
{
	p->StartingOffset.QuadPart = 0;
	p->PartitionLength.QuadPart = dev->d_size;
	p->HiddenSectors = 0;
	p->PartitionNumber = 1;
	p->PartitionType = PARTITION_ENTRY_UNUSED;
	p->BootIndicator = FALSE;
	p->RecognizedPartition = TRUE;
	p->RewritePartition = FALSE;
}

static void fill_partition_info_ex(struct _PARTITION_INFORMATION_EX *p, struct block_device *dev)
{
	p->PartitionStyle = PARTITION_STYLE_MBR;
	p->StartingOffset.QuadPart = 0;
	p->PartitionLength.QuadPart = dev->d_size;
	p->PartitionNumber = 1;
	p->RewritePartition = FALSE;
	p->Mbr.PartitionType = PARTITION_EXTENDED;
	p->Mbr.BootIndicator = FALSE;
	p->Mbr.RecognizedPartition = TRUE;
	p->Mbr.HiddenSectors = 0;
}

static NTSTATUS windrbd_device_control(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	struct block_device_reference *ref = device->DeviceExtension;
	if (ref == NULL || ref->bdev == NULL) {
		printk(KERN_WARNING "Device %p accessed after it was deleted.\n", device);
		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		irp->IoStatus.Information = 0;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	struct block_device *dev = ref->bdev;
	struct _IO_STACK_LOCATION *s = IoGetCurrentIrpStackLocation(irp);
	NTSTATUS status = STATUS_SUCCESS;

	switch (s->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_DISK_GET_DRIVE_GEOMETRY:
		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _DISK_GEOMETRY)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		fill_drive_geometry((struct _DISK_GEOMETRY*) irp->AssociatedIrp.SystemBuffer, dev);
		irp->IoStatus.Information = sizeof(struct _DISK_GEOMETRY);
		break;

	case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _DISK_GEOMETRY_EX)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		struct _DISK_GEOMETRY_EX *g = irp->AssociatedIrp.SystemBuffer;
		fill_drive_geometry(&g->Geometry, dev);
		g->DiskSize.QuadPart = dev->d_size;
		g->Data[0] = 0;

		irp->IoStatus.Information = sizeof(struct _DISK_GEOMETRY_EX);
		break;

	case IOCTL_DISK_GET_LENGTH_INFO:
		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _GET_LENGTH_INFORMATION)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		struct _GET_LENGTH_INFORMATION *l = irp->AssociatedIrp.SystemBuffer;
		l->Length.QuadPart = dev->d_size;
		irp->IoStatus.Information = sizeof(struct _GET_LENGTH_INFORMATION);
		break;

	case IOCTL_DISK_MEDIA_REMOVAL:
		if (s->Parameters.DeviceIoControl.InputBufferLength < sizeof(struct _PREVENT_MEDIA_REMOVAL)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		struct _PREVENT_MEDIA_REMOVAL *r = irp->AssociatedIrp.SystemBuffer;
		dbg(KERN_INFO "DRBD: Request for %slocking media\n", r->PreventMediaRemoval ? "" : "un");

		dev->mechanically_locked = r->PreventMediaRemoval;

		irp->IoStatus.Information = 0;
		break;

	case IOCTL_DISK_GET_PARTITION_INFO:
		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _PARTITION_INFORMATION)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		struct _PARTITION_INFORMATION *p = irp->AssociatedIrp.SystemBuffer;
		fill_partition_info(p, dev);
		irp->IoStatus.Information = sizeof(struct _PARTITION_INFORMATION);
		break;

	case IOCTL_DISK_GET_PARTITION_INFO_EX:
		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _PARTITION_INFORMATION_EX)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		struct _PARTITION_INFORMATION_EX *pe = irp->AssociatedIrp.SystemBuffer;
		fill_partition_info_ex(pe, dev);
		irp->IoStatus.Information = sizeof(struct _PARTITION_INFORMATION_EX);
		break;

	case IOCTL_DISK_SET_PARTITION_INFO:
		if (s->Parameters.DeviceIoControl.InputBufferLength < sizeof(struct _SET_PARTITION_INFORMATION)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		struct _SET_PARTITION_INFORMATION *pi = irp->AssociatedIrp.SystemBuffer;
		dbg(KERN_INFO "Request to set partition type to %x\n", pi->PartitionType);
		irp->IoStatus.Information = 0;
		break;

	case IOCTL_DISK_IS_WRITABLE:
		break;	/* just return without error */

	case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
	{
		int length = dev->path_to_device.Length;
		struct _MOUNTDEV_NAME *name = irp->AssociatedIrp.SystemBuffer;
		int total_length = sizeof(struct _MOUNTDEV_NAME) - sizeof(name->Name) + length + sizeof(name->Name[0]);

		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _MOUNTDEV_NAME) - sizeof(name->Name)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		name->NameLength = length;
		if (s->Parameters.DeviceIoControl.OutputBufferLength < total_length) {
				/* Fill in only length, so mount manager knows
				 * how much space we need. */
			irp->IoStatus.Information = sizeof(struct _MOUNTDEV_NAME);
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		RtlCopyMemory(name->Name, dev->path_to_device.Buffer, length);
		name->Name[length / sizeof(name->Name[0])] = 0;

		irp->IoStatus.Information = total_length;
		break;
	}

	case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
	{
		int length = dev->mount_point.Length;
		struct _MOUNTDEV_SUGGESTED_LINK_NAME *mount_point = irp->AssociatedIrp.SystemBuffer;
		int total_length = sizeof(struct _MOUNTDEV_SUGGESTED_LINK_NAME) - sizeof(mount_point->Name) + length + sizeof(mount_point->Name[0]);

		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _MOUNTDEV_SUGGESTED_LINK_NAME) - sizeof(mount_point->Name)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		mount_point->UseOnlyIfThereAreNoOtherLinks = FALSE;
		mount_point->NameLength = length;
		if (s->Parameters.DeviceIoControl.OutputBufferLength < total_length) {
				/* Fill in only length, so mount manager knows
				 * how much space we need. */
			irp->IoStatus.Information = sizeof(struct _MOUNTDEV_SUGGESTED_LINK_NAME);
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		RtlCopyMemory(mount_point->Name, dev->mount_point.Buffer, length);
		mount_point->Name[length / sizeof(mount_point->Name[0])] = 0;

		irp->IoStatus.Information = total_length;
		break;
	}

	case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
	{
		char guid[64];
			/* generated by https://www.guidgen.com */
		status = RtlStringCbPrintfA(guid, sizeof(guid)-1, "b71d%04x-0aac-47f4-b6df-223a1c73eb2e", dev->minor);

		if (status != STATUS_SUCCESS)
			break;

		int length = strlen(guid);
		struct _MOUNTDEV_UNIQUE_ID *id = irp->AssociatedIrp.SystemBuffer;
		int total_length = sizeof(struct _MOUNTDEV_UNIQUE_ID) - sizeof(id->UniqueId) + length + sizeof(id->UniqueId[0]);

		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _MOUNTDEV_UNIQUE_ID) - sizeof(id->UniqueId)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		id->UniqueIdLength = length;
		if (s->Parameters.DeviceIoControl.OutputBufferLength < total_length) {
				/* Fill in only length, so mount manager knows
				 * how much space we need. */
			irp->IoStatus.Information = sizeof(struct _MOUNTDEV_UNIQUE_ID);
			status = STATUS_BUFFER_OVERFLOW;
			break;
		}
		RtlCopyMemory(id->UniqueId, guid, length);
		id->UniqueId[length] = 0;

		irp->IoStatus.Information = total_length;
		break;
	}

	case IOCTL_STORAGE_GET_HOTPLUG_INFO:
		struct _STORAGE_HOTPLUG_INFO *hotplug_info = 
			irp->AssociatedIrp.SystemBuffer;

		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _STORAGE_HOTPLUG_INFO)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		hotplug_info->Size = sizeof(struct _STORAGE_HOTPLUG_INFO);
			/* TODO: makes no difference for FAT, ... */
		hotplug_info->MediaRemovable = TRUE;
		hotplug_info->MediaHotplug = TRUE;
		hotplug_info->DeviceHotplug = TRUE;
/*		hotplug_info->MediaRemovable = FALSE;
		hotplug_info->MediaHotplug = FALSE;
		hotplug_info->DeviceHotplug = FALSE; */
		hotplug_info->WriteCacheEnableOverride = FALSE;
		
		irp->IoStatus.Information = sizeof(struct _STORAGE_HOTPLUG_INFO);
		status = STATUS_SUCCESS;
		break;

/*
	case IOCTL_STORAGE_QUERY_PROPERTY:
		struct _STORAGE_PROPERTY_QUERY *query =
			irp->AssociatedIrp.SystemBuffer;

		if (s->Parameters.DeviceIoControl.InputBufferLength < sizeof(struct _STORAGE_PROPERTY_QUERY)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		dbg("IOCTL_STORAGE_QUERY_PROPERTY: PropertyId: %d QueryType: %d\n", query->PropertyId, query->QueryType);
		status = STATUS_NOT_IMPLEMENTED;
		break;
*/

	case IOCTL_DISK_CHECK_VERIFY:
	case IOCTL_STORAGE_CHECK_VERIFY:
	case IOCTL_STORAGE_CHECK_VERIFY2:
		dbg("CHECK_VERIFY (%x)\n", s->Parameters.DeviceIoControl.IoControlCode);
		if (s->Parameters.DeviceIoControl.OutputBufferLength >=
			sizeof(ULONG))
		{
			*(PULONG)irp->AssociatedIrp.SystemBuffer = 0;
			irp->IoStatus.Information = sizeof(ULONG);
		}
		status = STATUS_SUCCESS;
		break;

	case IOCTL_STORAGE_GET_DEVICE_NUMBER:
		struct _STORAGE_DEVICE_NUMBER *dn;
		if (s->Parameters.DeviceIoControl.OutputBufferLength < sizeof(struct _STORAGE_DEVICE_NUMBER)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		dn = (struct _STORAGE_DEVICE_NUMBER*) irp->AssociatedIrp.SystemBuffer;

		dn->DeviceType = FILE_DEVICE_DISK; /* TODO: device->DeviceType? */
		dn->DeviceNumber = dev->minor;
		dn->PartitionNumber = -1;

		irp->IoStatus.Information = sizeof(struct _STORAGE_DEVICE_NUMBER);
		status = STATUS_SUCCESS;
		break;

	case IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES:
		struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES *attrs =
	            (struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES*) irp->AssociatedIrp.SystemBuffer;

		if ((s->Parameters.DeviceIoControl.InputBufferLength <
	            sizeof(struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES)) ||
                   (s->Parameters.DeviceIoControl.InputBufferLength <
                   (attrs->DataSetRangesOffset + attrs->DataSetRangesLength))) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		dbg("attrs->Action is %d\n", attrs->Action);
		if (attrs->Action != DeviceDsmAction_Trim) {
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
		int items = attrs->DataSetRangesLength / sizeof(DEVICE_DATA_SET_RANGE);

		dbg("%d items\n", items);

		status = STATUS_SUCCESS;
		irp->IoStatus.Information = 0;
			/* TODO: trim */

		break;

	default: 
		dbg(KERN_DEBUG "DRBD IoCtl request not implemented: IoControlCode: 0x%x\n", s->Parameters.DeviceIoControl.IoControlCode);
		status = STATUS_INVALID_DEVICE_REQUEST;
	}

	irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return status;
}

static NTSTATUS windrbd_create(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_SUCCESS;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	struct block_device_reference *ref = device->DeviceExtension;
	if (ref == NULL || ref->bdev == NULL) {
		printk(KERN_WARNING "Device %p accessed after it was deleted.\n", device);
		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		irp->IoStatus.Information = 0;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	struct block_device *dev = ref->bdev;
	struct _IO_STACK_LOCATION *s = IoGetCurrentIrpStackLocation(irp);
	int mode;
	NTSTATUS status;
	int err;

	if (dev->drbd_device != NULL) {
		dbg(KERN_DEBUG "s->Parameters.Create.SecurityContext->DesiredAccess is %x\n", s->Parameters.Create.SecurityContext->DesiredAccess);

		mode = (s->Parameters.Create.SecurityContext->DesiredAccess &
       	               (FILE_WRITE_DATA  | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_APPEND_DATA | GENERIC_WRITE)) ? FMODE_WRITE : 0;

		dbg(KERN_INFO "DRBD device create request: opening DRBD device %s\n",
			mode == 0 ? "read-only" : "read-write");

		err = drbd_open(dev, mode);
		dbg(KERN_DEBUG "drbd_open returned %d\n", err);
		status = (err < 0) ? STATUS_INVALID_DEVICE_REQUEST : STATUS_SUCCESS;
	} else {
			/* If we are currently mounting we most likely got
			 * this IRP from the mount manager. Do not open the
			 * device in drbd, this will fail at this early stage.
			 */

		dbg("Create request while device isn't set up yet.\n");
		status = STATUS_SUCCESS;
	}

	irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
	dbg(KERN_DEBUG "status is %x\n", status);
	return status;
}


static NTSTATUS windrbd_close(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_SUCCESS;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	struct block_device_reference *ref = device->DeviceExtension;
	if (ref == NULL || ref->bdev == NULL) {
		printk(KERN_WARNING "Device %p accessed after it was deleted.\n", device);
		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		irp->IoStatus.Information = 0;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	struct block_device *dev = ref->bdev;
	struct _IO_STACK_LOCATION *s = IoGetCurrentIrpStackLocation(irp);
	int mode;
	NTSTATUS status;
	int err;

	if (dev->drbd_device != NULL) {
		mode = 0;	/* TODO: remember mode from open () */
/*	mode = (s->Parameters.Create.SecurityContext->DesiredAccess &
                (FILE_WRITE_DATA  | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_APPEND_DATA | GENERIC_WRITE)) ? FMODE_WRITE : 0; */

/*
		dbg(KERN_INFO "DRBD device close request: releasing DRBD device %s\n",
			mode == 0 ? "read-only" : "read-write");
*/

		err = dev->bd_disk->fops->release(dev->bd_disk, mode);
		dbg(KERN_DEBUG "drbd_release returned %d\n", err);
		status = (err < 0) ? STATUS_INVALID_DEVICE_REQUEST : STATUS_SUCCESS;
	} else {
		dbg("Close request while device isn't set up yet.\n");
			/* See comment in windrbd_create() */
		status = STATUS_SUCCESS;
	}

	irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

static NTSTATUS windrbd_cleanup(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_SUCCESS;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	struct block_device_reference *ref = device->DeviceExtension;
	if (ref == NULL || ref->bdev == NULL) {
		printk(KERN_WARNING "Device %p accessed after it was deleted.\n", device);
		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		irp->IoStatus.Information = 0;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	struct block_device *dev = ref->bdev;
	NTSTATUS status = STATUS_SUCCESS;

	dbg(KERN_INFO "Pretending that cleanup does something.\n");
	irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

/* Limit imposed by DRBD over the wire protocol. This will not change
 * in the next 5+ years, most likely never.
 */

#define MAX_BIO_SIZE (1024*1024)

static void windrbd_bio_finished(struct bio * bio, int error)
{
	PIRP irp = bio->bi_upper_irp;
	int i;
	NTSTATUS status;

printk("1 bio: %p bio->bi_common_data: %p irp: %p\n", bio, bio->bi_common_data, irp);
	status = STATUS_SUCCESS;
	if (error == 0) {
		if (bio->bi_rw == READ) {
			if (!bio->bi_common_data->bc_device_failed && bio->bi_upper_irp && bio->bi_upper_irp->MdlAddress) {
				char *user_buffer = MmGetSystemAddressForMdlSafe(bio->bi_upper_irp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
				if (user_buffer != NULL) {
					int offset;

					offset = bio->bi_mdl_offset;
					for (i=0;i<bio->bi_vcnt;i++) {
						RtlCopyMemory(user_buffer+offset, ((char*)bio->bi_io_vec[i].bv_page->addr)+bio->bi_io_vec[i].bv_offset, bio->bi_io_vec[i].bv_len);

						kfree(bio->bi_io_vec[i].bv_page->addr);
						offset += bio->bi_io_vec[i].bv_len;
					}
				} else {
					printk(KERN_WARNING "MmGetSystemAddressForMdlSafe returned NULL\n");
					status = STATUS_INVALID_PARAMETER;
				}
			}
		}
	} else {
		printk(KERN_ERR "I/O failed with %d\n", error);

			/* TODO: On Windows 7, this error seems not
			 * to reach userspace. On Windows 10, returning
			 * STATUS_UNSUCCESSFUL translates to a
			 * Permission denied error.
			 */
		// irp->IoStatus.Status = STATUS_NO_MEDIA_IN_DEVICE;
		status = STATUS_UNSUCCESSFUL;
	}
printk("2\n");
        unsigned long flags;

        spin_lock_irqsave(&bio->bi_common_data->bc_device_failed_lock, flags);
        int num_completed = atomic_inc_return(&bio->bi_common_data->bc_num_completed);
        int device_failed = bio->bi_common_data->bc_device_failed;
        if (status != STATUS_SUCCESS)
                bio->bi_common_data->bc_device_failed = 1;
        spin_unlock_irqrestore(&bio->bi_common_data->bc_device_failed_lock, flags);
printk("3\n");

	if (!device_failed && (status != STATUS_SUCCESS || num_completed == bio->bi_common_data->bc_num_requests)) {
printk("4\n");
		if (status == STATUS_SUCCESS)
			irp->IoStatus.Information = bio->bi_common_data->bc_total_size;
		else
			irp->IoStatus.Information = 0; /* TODO: one day also report partial I/O */
printk("5\n");
		irp->IoStatus.Status = status;
printk("6\n");
// if (status == STATUS_SUCCESS)
		IoCompleteRequest(irp, status != STATUS_SUCCESS ? IO_NO_INCREMENT : IO_DISK_INCREMENT);
// else
// printk("I/O failed with status %x, not completing IRP.\n", status);

/* else don't for now. BSOD? */
printk("7\n");
	}

	/* TODO: leaks bio->bi_common_data */

printk("8\n");
	for (i=0;i<bio->bi_vcnt;i++)
		kfree(bio->bi_io_vec[i].bv_page);

printk("9\n");
//	bio_put(bio);
printk("a\n");
}

static NTSTATUS make_drbd_requests(struct _IRP *irp, struct block_device *dev)
{
	struct _IO_STACK_LOCATION *s = IoGetCurrentIrpStackLocation(irp);
	struct _MDL *mdl = irp->MdlAddress;
	struct bio *bio;

	int vcnt;
	int last_size;
	int this_size;
	unsigned int total_size;
	sector_t sector;
	int i, b;
	char *buffer;
	struct bio_collection *common_data;

printk("1\n");

	if (s == NULL) {
		printk("Stacklocation is NULL.\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	if (mdl == NULL) {
			/* TODO: this sometimes happens with windrbd-test.
			 * Find out why.
			 */
		dbg("MdlAddress is NULL.\n");
		return STATUS_INVALID_PARAMETER;
	}

	/* later have more than one .. */

	/* Update: I tried to generate this test case using ReadFileGather/
	 * WriteFileScatter but this is more like a mmap replacement (has
	 * one MDL element with page table entries created for each vector
	 * element). I don't know currently how to test this. Plus we
	 * found a Windows block device that blue screens (!) if there
	 * is more than one MDL element in the request (Windows 10 USB
	 * storage driver). For now, it should be sufficient to support
	 * one MDL element, we will implement this if someone complains.
	 */

	if (mdl->Next != NULL) {
		printk(KERN_ERR "not implemented: have more than one mdl. Dropping additional mdl data.\n");
		return STATUS_NOT_IMPLEMENTED;
	}

	if (s->MajorFunction == IRP_MJ_WRITE) {
		total_size = s->Parameters.Write.Length;
		sector = (s->Parameters.Write.ByteOffset.QuadPart) / dev->bd_block_size;
	} else if (s->MajorFunction == IRP_MJ_READ) {
		total_size = s->Parameters.Read.Length;
		sector = (s->Parameters.Read.ByteOffset.QuadPart) / dev->bd_block_size;
	} else {
		printk("s->MajorFunction neither read nor write.\n");
		return STATUS_INVALID_PARAMETER;
	}
	if (sector * dev->bd_block_size >= dev->d_size) {
		dbg("Attempt to read past the end of the device\n");
		return STATUS_INVALID_PARAMETER;
	}
	if (sector * dev->bd_block_size + total_size > dev->d_size) {
		dbg("Attempt to read past the end of the device, request shortened\n");
		total_size = dev->d_size - sector * dev->bd_block_size; 
	}
	if (total_size == 0) {
		printk("I/O request of size 0.\n");
		return STATUS_INVALID_PARAMETER;
	}

		/* Address returned by MmGetSystemAddressForMdlSafe
		 * is already offset, not using MmGetMdlByteOffset.
		 */

	buffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
	if (buffer == NULL) {
		printk("I/O buffer from MmGetSystemAddressForMdlSafe() is NULL\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
// dbg("%s sector: %d this_bio_size: %d\n", s->MajorFunction == IRP_MJ_WRITE ? "WRITE" : "READ", sector, this_bio_size);
	int bio_count = (total_size-1) / MAX_BIO_SIZE + 1;
	int this_bio_size;
	int last_bio_size = total_size % MAX_BIO_SIZE;
	if (last_bio_size == 0)
		last_bio_size = MAX_BIO_SIZE;

	common_data = kzalloc(sizeof(*common_data), 0, 'DRBD');
	if (common_data == NULL) {
		printk("Cannot allocate common data.\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	atomic_set(&common_data->bc_num_completed, 0);
	common_data->bc_total_size = total_size;
	common_data->bc_num_requests = bio_count;
	common_data->bc_device_failed = 0;
	spin_lock_init(&common_data->bc_device_failed_lock);

	for (b=0; b<bio_count; b++) {
		this_bio_size = (b==bio_count-1) ? last_bio_size : MAX_BIO_SIZE;

		vcnt = (this_bio_size-1) / PAGE_SIZE + 1;
		last_size = this_bio_size % PAGE_SIZE;

		bio = bio_alloc(GFP_NOIO, vcnt, 'DBRD');
		if (bio == NULL) {
			printk("Couldn't allocate bio.\n");
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		bio->bi_rw = s->MajorFunction == IRP_MJ_WRITE ? WRITE : READ;
		bio->bi_bdev = dev;
		bio->bi_max_vecs = vcnt;
		bio->bi_vcnt = vcnt;
		bio->bi_paged_memory = bio->bi_rw == WRITE;
		bio->bi_size = this_bio_size;
		bio->bi_sector = sector + b*MAX_BIO_SIZE/dev->bd_block_size;
		bio->bi_mdl_offset = b*MAX_BIO_SIZE;
		bio->bi_common_data = common_data;

// dbg("%s sector: %d total_size: %d\n", s->MajorFunction == IRP_MJ_WRITE ? "WRITE" : "READ", sector, total_size);

		for (i=0; i<vcnt; i++) {
			this_size = (i == vcnt-1) ? last_size : PAGE_SIZE;
			if (this_size == 0)
				this_size = PAGE_SIZE;

			bio->bi_io_vec[i].bv_page = kzalloc(sizeof(struct page), 0, 'DRBD');
			if (bio->bi_io_vec[i].bv_page == NULL) {
				printk("Couldn't allocate page.\n");
				return STATUS_INSUFFICIENT_RESOURCES; /* TODO: cleanup */
			}

			bio->bi_io_vec[i].bv_len = this_size;

/*
 * TODO: eventually we want to make READ requests work without the
 *	 intermediate buffer and the extra copy.
 */

			if (bio->bi_rw == READ)
				bio->bi_io_vec[i].bv_page->addr = kmalloc(this_size, 0, 'DRBD');
			else
				bio->bi_io_vec[i].bv_page->addr = buffer+bio->bi_mdl_offset+i*PAGE_SIZE;

			if (bio->bi_io_vec[i].bv_page->addr == NULL) {
				printk("Couldn't allocate temp buffer for read.\n");
				return STATUS_INSUFFICIENT_RESOURCES; /* TODO: cleanup */
			}

			bio->bi_io_vec[i].bv_offset = 0;
		}
		bio->bi_end_io = windrbd_bio_finished;
		bio->bi_upper_irp = irp;

// dbg("bio: %p bio->bi_io_vec[0].bv_page->addr: %p bio->bi_io_vec[0].bv_len: %d bio->bi_io_vec[0].bv_offset: %d\n", bio, bio->bi_io_vec[0].bv_page->addr, bio->bi_io_vec[0].bv_len, bio->bi_io_vec[0].bv_offset);
// dbg("bio->bi_size: %d bio->bi_sector: %d bio->bi_mdl_offset: %d\n", bio->bi_size, bio->bi_sector, bio->bi_mdl_offset); 

	/* Do this before windrbd_bio_finished might be called, else
	 * this could produce a blue screen.
	 */

        IoMarkIrpPending(irp);

printk("2\n");
		drbd_make_request(dev->drbd_device->rq_queue, bio);
printk("3\n");
	}
printk("4\n");

	return STATUS_SUCCESS;
}

static NTSTATUS windrbd_io(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
printk("1\n");
	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_SUCCESS;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	struct block_device_reference *ref = device->DeviceExtension;
	if (ref == NULL || ref->bdev == NULL) {
		printk(KERN_WARNING "I/O request: Device %p accessed after it was deleted.\n", device);
		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		irp->IoStatus.Information = 0;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	struct block_device *dev = ref->bdev;
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

		/* Happens when mounting fails and we try to umount
		 * the device.
		 */

	if (dev->drbd_device == NULL) {
		dbg("I/O request while device isn't set up yet.\n");
		goto exit;
	}

	if (dev->drbd_device->resource->role[NOW] != R_PRIMARY) {
		dbg("I/O request while not primary.\n");
		goto exit;
	}

		/* allow I/O when the local disk failed, usually there
		 * are peers which can handle the I/O. If not, DRBD will
		 * report an I/O error which we will get in our completion
		 * routine later and can report to the application.
		 */

printk("2\n");
	status = make_drbd_requests(irp, dev);
	if (status != STATUS_SUCCESS)
		goto exit;
printk("3\n");

	return STATUS_PENDING;

exit:
printk("4\n");
	irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
printk("5\n");

        return status;
}

static NTSTATUS windrbd_shutdown(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_SUCCESS;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	printk("System shutdown, for now, don't clean up, there might be DRBD resources online\nin which case we would crash the system.\n");

	printk("device: %p irp: %p\n", device, irp);
#if 0
	if (device == mvolRootDeviceObject)
		drbd_cleanup();

/* TODO: clean up logging. */
#endif

	irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(irp, IO_NO_INCREMENT);

        return STATUS_SUCCESS;
}

static void windrbd_bio_flush_finished(struct bio * bio, int error)
{
	PIRP irp = bio->bi_upper_irp;

	if (error == 0) {
		irp->IoStatus.Information = bio->bi_size;
		irp->IoStatus.Status = STATUS_SUCCESS;
	} else {
		printk(KERN_ERR "Flush failed with %d\n", error);
		irp->IoStatus.Information = 0;

			/* TODO: On Windows 7, this error seems not
			 * to reach userspace. On Windows 10, returning
			 * STATUS_UNSUCCESSFUL translates to a
			 * Permission denied error.
			 */
		// irp->IoStatus.Status = STATUS_NO_MEDIA_IN_DEVICE;
		irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
	}
	IoCompleteRequest(irp, error ? IO_NO_INCREMENT : IO_DISK_INCREMENT);

	bio_put(bio);
}

static NTSTATUS windrbd_flush(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_SUCCESS;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	struct block_device_reference *ref = device->DeviceExtension;
	if (ref == NULL || ref->bdev == NULL) {
		printk(KERN_WARNING "Device %p accessed after it was deleted.\n", device);
		irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		irp->IoStatus.Information = 0;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	struct block_device *dev = ref->bdev;
	struct bio *bio;
	NTSTATUS status;

	bio = bio_alloc(GFP_NOIO, 0, 'DBRD');
	if (bio == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}
	bio->bi_rw = WRITE | DRBD_REQ_PREFLUSH;
	bio->bi_size = 0;
	bio->bi_end_io = windrbd_bio_flush_finished;
	bio->bi_upper_irp = irp;
	bio->bi_bdev = dev;

        IoMarkIrpPending(irp);
	drbd_make_request(dev->drbd_device->rq_queue, bio);
		/* The irp may be already invalid here. */
	return STATUS_PENDING;

exit:
	irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);

        return status;
}

static NTSTATUS windrbd_pnp(struct _DEVICE_OBJECT *device, struct _IRP *irp)
{
	NTSTATUS status;

	if (device == mvolRootDeviceObject) {
		printk(KERN_DEBUG "Root device request.\n");

		irp->IoStatus.Status = STATUS_SUCCESS;
	        IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}
	status = STATUS_NOT_IMPLEMENTED;

	dbg("Pnp: device: %p irp: %p\n", device, irp);

	struct _IO_STACK_LOCATION *s = IoGetCurrentIrpStackLocation(irp);
	
	dbg(KERN_DEBUG "PnP device request not implemented: MajorFunction: 0x%x, MinorFunction: %x\n", s->MajorFunction, s->MinorFunction);
	if (s->MinorFunction == IRP_MN_QUERY_DEVICE_RELATIONS) {
		dbg("Pnp: Is a IRP_MN_QUERY_DEVICE_RELATIONS: s->Parameters.QueryDeviceRelations.Type is %x\n", s->Parameters.QueryDeviceRelations.Type);

#if 0

		struct _DEVICE_RELATIONS *rel;
		rel = kmalloc(sizeof(*rel), 0, 'DRBD');
		if (rel != NULL) {
			rel->Count = 1; /* blue screens if this is 0 */
			rel->Objects[0] = device; /* blue screens because this is not a PDO (yet). Hmmm ... */
			ObReferenceObject(device);
			irp->IoStatus.Information = (ULONG_PTR) rel;
			status = STATUS_SUCCESS;
		}
#endif
	}

	irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

void windrbd_set_major_functions(struct _DRIVER_OBJECT *obj)
{
	int i;
	NTSTATUS status;

	for (i=0; i<IRP_MJ_MAXIMUM_FUNCTION; i++)
		obj->MajorFunction[i] = windrbd_not_implemented;

	obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = windrbd_device_control;
	obj->MajorFunction[IRP_MJ_READ] = windrbd_io;
	obj->MajorFunction[IRP_MJ_WRITE] = windrbd_io;
	obj->MajorFunction[IRP_MJ_CREATE] = windrbd_create;
	obj->MajorFunction[IRP_MJ_CLOSE] = windrbd_close;
	obj->MajorFunction[IRP_MJ_CLEANUP] = windrbd_cleanup;
	obj->MajorFunction[IRP_MJ_PNP] = windrbd_pnp;
	obj->MajorFunction[IRP_MJ_SHUTDOWN] = windrbd_shutdown;
	obj->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = windrbd_flush;

	status = IoRegisterShutdownNotification(mvolRootDeviceObject);
	if (status != STATUS_SUCCESS) {
		printk("Could not register shutdown notification.\n");
	}
}
