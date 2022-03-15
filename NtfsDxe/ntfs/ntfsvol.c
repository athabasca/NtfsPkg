/**
 * ntfs.c - Simple functionality for startup, mounting and unmounting of NTFS-based devices.
 *
 * Copyright (c) 2009 Rhys "Shareese" Koedijk
 * Copyright (c) 2006 Michael "Chishm" Chisholm
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "types.h"

#include "ntfs.h"
#include "ntfsinternal.h"
#include "ntfsfile.h"
#include "ntfsdir.h"
#include "gekko_io.h"
#include "cache.h"
#include "mem_allocate.h"

// NTFS device driver devoptab
static const devoptab_t devops_ntfs = {
    NULL, /* Device name */
    sizeof (ntfs_file_state),
    ntfs_open_r,
    ntfs_close_r,
    ntfs_write_r,
    ntfs_read_r,
    ntfs_seek_r,
    ntfs_fstat_r,
    ntfs_stat_r,
    ntfs_link_r,
    ntfs_unlink_r,
    ntfs_chdir_r,
    ntfs_rename_r,
    ntfs_mkdir_r,
    sizeof (ntfs_dir_state),
    ntfs_diropen_r,
    ntfs_dirreset_r,
    ntfs_dirnext_r,
    ntfs_dirclose_r,
    ntfs_statvfs_r,
    ntfs_ftruncate_r,
    ntfs_fsync_r,
    NULL /* Device data */
};

void ntfsInit (void)
{
    static bool isInit = false;

    // Initialise ntfs-3g (if not already done so)
    if (!isInit) {
        isInit = true;

        // Set the log handler
        #ifdef NTFS_ENABLE_LOG
        ntfs_log_set_handler(ntfs_log_handler_stderr);
        #else
        ntfs_log_set_handler(ntfs_log_handler_null);
        #endif
    }

    return;
}

//int ntfsFindPartitions (NTFS_VOUME, sec_t **partitions)
//{
//    MASTER_BOOT_RECORD mbr;
//    PARTITION_RECORD *partition = NULL;
//    sec_t partition_starts[NTFS_MAX_PARTITIONS] = {0};
//    int partition_count = 0;
//    sec_t part_lba = 0;
//    int i;
//
//    union {
//        u8 buffer[MAX_SECTOR_SIZE];
//        MASTER_BOOT_RECORD mbr;
//        EXTENDED_BOOT_RECORD ebr;
//        NTFS_BOOT_SECTOR boot;
//    } sector;
//
//    // Sanity check
//    if (!interface) {
//        errno = EINVAL;
//        return -1;
//    }
//    if (!partitions)
//        return 0;
//
//    // Initialise ntfs-3g
//    ntfsInit();
//
//    // Start the device and check that it is inserted
//    if (!interface->startup()) {
//        errno = EIO;
//        return -1;
//    }
//    if (!interface->isInserted()) {
//        return 0;
//    }
//
//    // Read the first sector on the device
//    if (!interface->readSectors(0, 1, &sector.buffer)) {
//        errno = EIO;
//        return -1;
//    }
//
//    // If this is the devices master boot record
//    if (sector.mbr.signature == MBR_SIGNATURE) {
//        memcpy(&mbr, &sector, sizeof(MASTER_BOOT_RECORD));
//        ntfs_log_debug("Valid Master Boot Record found\n");
//
//        // Search the partition table for all NTFS partitions (max. 4 primary partitions)
//        for (i = 0; i < 4; i++) {
//            partition = &mbr.partitions[i];
//            part_lba = le32_to_cpu(mbr.partitions[i].lba_start);
//
//            ntfs_log_debug("Partition %i: %s, sector %d, type 0x%x\n", i + 1,
//                           partition->status == PARTITION_STATUS_BOOTABLE ? "bootable (active)" : "non-bootable",
//                           part_lba, partition->type);
//
//            // Figure out what type of partition this is
//            switch (partition->type) {
//
//                // Ignore empty partitions
//                case PARTITION_TYPE_EMPTY:
//                    continue;
//
//                // NTFS partition
//                case PARTITION_TYPE_NTFS: {
//                    ntfs_log_debug("Partition %i: Claims to be NTFS\n", i + 1);
//
//                    // Read and validate the NTFS partition
//                    if (interface->readSectors(part_lba, 1, &sector)) {
//                        if (sector.boot.oem_id == NTFS_OEM_ID) {
//                            ntfs_log_debug("Partition %i: Valid NTFS boot sector found\n", i + 1);
//                            if (partition_count < NTFS_MAX_PARTITIONS) {
//                                partition_starts[partition_count] = part_lba;
//                                partition_count++;
//                            }
//                        } else {
//                            ntfs_log_debug("Partition %i: Invalid NTFS boot sector, not actually NTFS\n", i + 1);
//                        }
//                    }
//
//                    break;
//
//                }
//
//                // DOS 3.3+ or Windows 95 extended partition
//                case PARTITION_TYPE_DOS33_EXTENDED:
//                case PARTITION_TYPE_WIN95_EXTENDED: {
//                    ntfs_log_debug("Partition %i: Claims to be Extended\n", i + 1);
//
//                    // Walk the extended partition chain, finding all NTFS partitions within it
//                    sec_t ebr_lba = part_lba;
//                    sec_t next_erb_lba = 0;
//                    do {
//
//                        // Read and validate the extended boot record
//                        if (interface->readSectors(ebr_lba + next_erb_lba, 1, &sector)) {
//                            if (sector.ebr.signature == EBR_SIGNATURE) {
//                                ntfs_log_debug("Logical Partition @ %d: %s type 0x%x\n", ebr_lba + next_erb_lba,
//                                               sector.ebr.partition.status == PARTITION_STATUS_BOOTABLE ? "bootable (active)" : "non-bootable",
//                                               sector.ebr.partition.type);
//
//                                // Get the start sector of the current partition
//                                // and the next extended boot record in the chain
//                                part_lba = ebr_lba + next_erb_lba + le32_to_cpu(sector.ebr.partition.lba_start);
//                                next_erb_lba = le32_to_cpu(sector.ebr.next_ebr.lba_start);
//
//                                // Check if this partition has a valid NTFS boot record
//                                if (interface->readSectors(part_lba, 1, &sector)) {
//                                    if (sector.boot.oem_id == NTFS_OEM_ID) {
//                                        ntfs_log_debug("Logical Partition @ %d: Valid NTFS boot sector found\n", part_lba);
//                                        if(sector.ebr.partition.type != PARTITION_TYPE_NTFS) {
//                                            ntfs_log_warning("Logical Partition @ %d: Is NTFS but type is 0x%x; 0x%x was expected\n", part_lba, sector.ebr.partition.type, PARTITION_TYPE_NTFS);
//                                        }
//                                        if (partition_count < NTFS_MAX_PARTITIONS) {
//                                            partition_starts[partition_count] = part_lba;
//                                            partition_count++;
//                                        }
//                                    }
//                                }
//
//                            } else {
//                                next_erb_lba = 0;
//                            }
//                        }
//
//                    } while (next_erb_lba);
//
//                    break;
//
//                }
//
//                // Unknown or unsupported partition type
//                default: {
//
//                    // Check if this partition has a valid NTFS boot record anyway,
//                    // it might be misrepresented due to a lazy partition editor
//                    if (interface->readSectors(part_lba, 1, &sector)) {
//                        if (sector.boot.oem_id == NTFS_OEM_ID) {
//                            ntfs_log_debug("Partition %i: Valid NTFS boot sector found\n", i + 1);
//                            if(partition->type != PARTITION_TYPE_NTFS) {
//                                ntfs_log_warning("Partition %i: Is NTFS but type is 0x%x; 0x%x was expected\n", i + 1, partition->type, PARTITION_TYPE_NTFS);
//                            }
//                            if (partition_count < NTFS_MAX_PARTITIONS) {
//                                partition_starts[partition_count] = part_lba;
//                                partition_count++;
//                            }
//                        }
//                    }
//
//                    break;
//
//                }
//
//            }
//
//        }
//
//    // Else it is assumed this device has no master boot record
//    } else {
//        ntfs_log_debug("No Master Boot Record was found!\n");
//
//        // As a last-ditched effort, search the first 64 sectors of the device for stray NTFS partitions
//        for (i = 0; i < 64; i++) {
//            if (interface->readSectors(i, 1, &sector)) {
//                if (sector.boot.oem_id == NTFS_OEM_ID) {
//                    ntfs_log_debug("Valid NTFS boot sector found at sector %d!\n", i);
//                    if (partition_count < NTFS_MAX_PARTITIONS) {
//                        partition_starts[partition_count] = i;
//                        partition_count++;
//                    }
//                }
//            }
//        }
//
//    }
//
//    // Return the found partitions (if any)
//    if (partition_count > 0) {
//        *partitions = (sec_t*)ntfs_alloc(sizeof(sec_t) * partition_count);
//        if (*partitions) {
//            memcpy(*partitions, &partition_starts, sizeof(sec_t) * partition_count);
//            return partition_count;
//        }
//    }
//
//    return 0;
//}

	//int ntfsMountAll (ntfs_md **mounts, u32 flags)
	//{
	//const INTERFACE_ID *discs = ntfsGetDiscInterfaces();
	//const INTERFACE_ID *disc = NULL;
	//ntfs_md mount_points[NTFS_MAX_MOUNTS];
	//sec_t *partitions = NULL;
	//int mount_count = 0;
	//int partition_count = 0;
	//char name[128];
	//int i, j, k;

	//// Initialise ntfs-3g
	//ntfsInit();

	//// Find and mount all NTFS partitions on all known devices
	//for (i = 0; discs[i].name != NULL && discs[i].interface != NULL; i++) {
	//	disc = &discs[i];
	//	partition_count = ntfsFindPartitions(disc->interface, &partitions);
	//	if (partition_count > 0 && partitions) {
	//		for (j = 0, k = 0; j < partition_count; j++) {

	//			// Find the next unused mount name
	//			do {
	//				sprintf(name, "%s%i", NTFS_MOUNT_PREFIX, k++);
	//				if (k >= NTFS_MAX_MOUNTS) {
	//					ntfs_free(partitions);
	//					errno = EADDRNOTAVAIL;
	//					return -1;
	//				}
	//			} while (ntfsGetDevice(name, false));

	//			// Mount the partition
	//			if (mount_count < NTFS_MAX_MOUNTS) {
	//				if (ntfsMount(name, disc->interface, partitions[j], CACHE_DEFAULT_PAGE_SIZE, CACHE_DEFAULT_PAGE_COUNT, flags)) {
	//					strcpy(mount_points[mount_count].name, name);
	//					mount_points[mount_count].interface = disc->interface;
	//					mount_points[mount_count].startSector = partitions[j];
	//					mount_count++;
	//				}
	//			}

	//		}
	//		ntfs_free(partitions);
	//	}
	//}

	//// Return the mounts (if any)
	//if (mount_count > 0 && mounts) {
	//	*mounts = (ntfs_md*)ntfs_alloc(sizeof(ntfs_md) * mount_count);
	//	if (*mounts) {
	//		memcpy(*mounts, &mount_points, sizeof(ntfs_md) * mount_count);
	//		return mount_count;
	//	}
	//}

	//return 0;
	//}
//
//int ntfsMountDevice (const DISC_INTERFACE *interface, ntfs_md **mounts, u32 flags)
//{
//    const INTERFACE_ID *discs = ntfsGetDiscInterfaces();
//    const INTERFACE_ID *disc = NULL;
//    ntfs_md mount_points[NTFS_MAX_MOUNTS];
//    sec_t *partitions = NULL;
//    int mount_count = 0;
//    int partition_count = 0;
//    char name[128];
//    int i, j, k;
//
//    // Sanity check
//    if (!interface) {
//        errno = EINVAL;
//        return -1;
//    }
//
//    // Initialise ntfs-3g
//    ntfsInit();
//
//    // Find the specified device then find and mount all NTFS partitions on it
//    for (i = 0; discs[i].name != NULL && discs[i].interface != NULL; i++) {
//        if (discs[i].interface == interface) {
//            disc = &discs[i];
//            partition_count = ntfsFindPartitions(disc->interface, &partitions);
//            if (partition_count > 0 && partitions) {
//                for (j = 0, k = 0; j < partition_count; j++) {
//
//                    // Find the next unused mount name
//                    do {
//                        sprintf(name, "%s%i", NTFS_MOUNT_PREFIX, k++);
//                        if (k >= NTFS_MAX_MOUNTS) {
//                            ntfs_free(partitions);
//                            errno = EADDRNOTAVAIL;
//                            return -1;
//                        }
//                    } while (ntfsGetDevice(name, false));
//
//                    // Mount the partition
//                    if (mount_count < NTFS_MAX_MOUNTS) {
//                        if (ntfsMount(name, disc->interface, partitions[j], CACHE_DEFAULT_PAGE_SIZE, CACHE_DEFAULT_PAGE_COUNT, flags)) {
//                            strcpy(mount_points[mount_count].name, name);
//                            mount_points[mount_count].interface = disc->interface;
//                            mount_points[mount_count].startSector = partitions[j];
//                            mount_count++;
//                        }
//                    }
//
//                }
//                ntfs_free(partitions);
//            }
//            break;
//        }
//    }
//
//    // If we couldn't find the device then return with error status
//    if (!disc) {
//        errno = ENODEV;
//        return -1;
//    }
//
//    // Return the mounts (if any)
//    if (mount_count > 0 && mounts) {
//        *mounts = (ntfs_md*)ntfs_alloc(sizeof(ntfs_md) * mount_count);
//        if (*mounts) {
//            memcpy(*mounts, &mount_points, sizeof(ntfs_md) * mount_count);
//            return mount_count;
//        }
//    }
//
//    return 0;
//}

ntfs_vd *ntfsMount (const char *name, struct _NTFS_VOLUME *interface, sec_t startSector, u32 cachePageCount, u32 cachePageSize, u32 flags)
{
    ntfs_vd *vd = NULL;
    struct _uefi_fd *fd = NULL;
	const devoptab_t *mnt;

	Print(L"ntfsMount %a\n", name);

	//CpuBreakpoint();

	// Sanity check
    if (!name || !interface) {
		//Print(L"ntfsMount EINVAL\n");
        errno = EINVAL;
        return false;
    }

    // Initialise ntfs-3g
	ntfsInit();

	mnt = ntfsGetDevice(name, false);

    // Check that the requested mount name is free
    if (mnt) {
		Print(L"ntfsMount EADDRINUSE\n");
        errno = 99; //EADDRINUSE;
	
		return (ntfs_vd*) mnt->deviceData;	// previous mnt data!
    }


    // Allocate the volume descriptor
    vd = (ntfs_vd*)ntfs_alloc(sizeof(ntfs_vd));
    if (!vd) {
		Print(L"ntfsMount ENOMEM\n");
        errno = ENOMEM;
        return false;
    }
	else {
		Print(L"ntfsMount ntfs_vd!\n");
	}

    // Setup the volume descriptor
	Print(L"vd.id! [%x]\n", vd);
    vd->id = 0;//interface->ioType;
    Print(L"vd.flags!\n");
	vd->flags = 0;
    vd->uid = 0;
    vd->gid = 0;
    vd->fmask = 0;
    vd->dmask = 0;
    vd->atime = ((flags & NTFS_UPDATE_ACCESS_TIMES) ? ATIME_ENABLED : ATIME_DISABLED);
    vd->showHiddenFiles = (flags & NTFS_SHOW_HIDDEN_FILES);
    vd->showSystemFiles = (flags & NTFS_SHOW_SYSTEM_FILES);

	Print(L"invoking ntfs_alloc!\n");
    // Allocate the device driver descriptor
    fd = (struct _uefi_fd *)ntfs_alloc(sizeof(struct _uefi_fd));
    if (!fd) {
		Print(L"ntfsMount ENOMEM(2)\n");
		ntfs_free(vd);
        errno = ENOMEM;
        return false;
    }
	else
	{
		Print(L"ntfs_alloc uefi_fd\n");
	}

    // Setup the device driver descriptor
    fd->interface = interface;
    fd->startSector = startSector;
    fd->sectorSize = 0x200;
	fd->sectorCount = 0x200;
    fd->cachePageCount = cachePageCount;
    fd->cachePageSize = cachePageSize;

    // Allocate the device driver
    vd->dev = ntfs_device_alloc(name, 0, &ntfs_device_uefi_io_ops, fd);
    if (!vd->dev) {
		Print(L"ntfsMount ntfs_device_alloc failed\n");
        ntfs_free(fd);
        ntfs_free(vd);
        return false;
    }
	Print(L"ntfs_device_alloc success\n");

    // Build the mount flags
    if (flags & NTFS_READ_ONLY)
    	vd->flags |= NTFS_MNT_RDONLY;
    
    if (flags & NTFS_RECOVER)
        vd->flags |= NTFS_MNT_RECOVER;
    if (flags & NTFS_IGNORE_HIBERFILE)
        vd->flags |= NTFS_MNT_IGNORE_HIBERFILE;

    if (vd->flags & NTFS_MNT_RDONLY)
        Print("Mounting \"%s\" as read-only\n", name);

    // Mount the device
	Print(L"Invoking ntfs_device_mount\n");
    vd->vol = ntfs_device_mount(vd->dev, vd->flags);
    if (!vd->vol) {
        switch(ntfs_volume_error(errno)) {
            case NTFS_VOLUME_NOT_NTFS: errno = EINVALPART; break;
            case NTFS_VOLUME_CORRUPT: errno = EINVALPART; break;
            case NTFS_VOLUME_HIBERNATED: errno = EHIBERNATED; break;
            case NTFS_VOLUME_UNCLEAN_UNMOUNT: errno = EDIRTY; break;
            default: errno = EINVAL; break;
        }
        ntfs_device_free(vd->dev);
        ntfs_free(vd);
		Print(L"ntfsMount ntfs_device_mount FAILED (%x)\n", errno);
        return NULL;
    }

	if (flags & NTFS_IGNORE_CASE)
		ntfs_set_ignore_case(vd->vol);

    // Initialise the volume descriptor
    if (ntfsInitVolume(vd)) {
        ntfs_umount(vd->vol, true);
        ntfs_free(vd);
		Print(L"ntfsMount ntfsInitVolume failed\n");
        return NULL;
    }

    // Add the device to the devoptab table
    if (ntfsAddDevice(name, vd)) {
		Print(L"ntfsMount ntfsAddDevice failed\n");
        ntfsDeinitVolume(vd);
        ntfs_umount(vd->vol, true);
        ntfs_free(vd);
        return NULL;
    }

	Print(L"ntfsMount done.\n");
    return vd;
}

void ntfsUnmount (const char *name, bool force)
{
    ntfs_vd *vd = NULL;

    // Get the devices volume descriptor
    vd = ntfsGetVolume(name);
    if (!vd)
        return;

    // Remove the device from the devoptab table
    ntfsRemoveDevice(name);

    // Deinitialise the volume descriptor
    ntfsDeinitVolume(vd);

    // Unmount the volume
    ntfs_umount(vd->vol, force);

    // Free the volume descriptor
    ntfs_free(vd);

    return;
}

const char *ntfsGetVolumeName (const char *name)
{
    ntfs_vd *vd = NULL;

    // Sanity check
    if (!name) {
        errno = EINVAL;
        return NULL;
    }

    // Get the devices volume descriptor
    vd = ntfsGetVolume(name);
    if (!vd) {
        errno = ENODEV;
        return NULL;
    }
    return vd->vol->vol_name;
}

bool ntfsSetVolumeName (const char *name, const char *volumeName)
{
    ntfs_vd *vd = NULL;
    ntfs_attr *na = NULL;
    ntfschar *ulabel = NULL;
    int ulabel_len;

    // Sanity check
    if (!name) {
        errno = EINVAL;
        return false;
    }

    // Get the devices volume descriptor
    vd = ntfsGetVolume(name);
    if (!vd) {
        errno = ENODEV;
        return false;
    }

    // Lock
    ntfsLock(vd);

    // Convert the new volume name to unicode
    ulabel_len = ntfsLocalToUnicode(volumeName, &ulabel) * sizeof(ntfschar);
    if (ulabel_len < 0) {
        ntfsUnlock(vd);
        errno = EINVAL;
        return false;
    }

    // Check if the volume name attribute exists
    na = ntfs_attr_open(vd->vol->vol_ni, AT_VOLUME_NAME, NULL, 0);
    if (na) {

        // It does, resize it to match the length of the new volume name
        if (ntfs_attr_truncate(na, ulabel_len)) {
            free(ulabel);
            ntfsUnlock(vd);
            return false;
        }

        // Write the new volume name
        if (ntfs_attr_pwrite(na, 0, ulabel_len, ulabel) != ulabel_len) {
            free(ulabel);
            ntfsUnlock(vd);
            return false;
        }

    } else {

        // It doesn't, create it now
        if (ntfs_attr_add(vd->vol->vol_ni, AT_VOLUME_NAME, NULL, 0, (u8*)ulabel, ulabel_len)) {
            free(ulabel);
            ntfsUnlock(vd);
            return false;
        }

    }

    // Reset the volumes name cache (as it has now been changed)
    vd->name[0] = '\0';

    // Close the volume name attribute
    if (na)
        ntfs_attr_close(na);

    // Sync the volume node
    if (ntfs_inode_sync(vd->vol->vol_ni)) {
        free(ulabel);
        ntfsUnlock(vd);
        return false;
    }

    // Clean up
    free(ulabel);

    // Unlock
    ntfsUnlock(vd);

    return true;
}

const devoptab_t *ntfsGetDevOpTab (void)
{
    return &devops_ntfs;
}
