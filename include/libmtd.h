/*
 * Copyright (C) 2008, 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Author: Artem Bityutskiy
 *
 * MTD library.
 */

#ifndef __LIBMTD_H__
#define __LIBMTD_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum MTD device name length */
#define MTD_NAME_MAX 127
/* Maximum MTD device type string length */
#define MTD_TYPE_MAX 64

/* MTD library descriptor */
typedef void * libmtd_t;

/* Forward decls */
struct region_info_user;
struct mtd_dev_info;

/**
 * @mtd_dev_cnt: count of MTD devices in system
 * @lowest_mtd_num: lowest MTD device number in system
 * @highest_mtd_num: highest MTD device number in system
 * @sysfs_supported: non-zero if sysfs is supported by MTD
 */
struct mtd_info
{
	int mtd_dev_cnt;
	int lowest_mtd_num;
	int highest_mtd_num;
	unsigned int sysfs_supported:1;
};

/**
 * struct mtd_pairing_info - page pairing information
 *
 * @pair: pair id
 * @group: group id
 *
 * The term "pair" is used here, even though TLC NANDs might group pages by 3
 * (3 bits in a single cell). A pair should regroup all pages that are sharing
 * the same cell. Pairs are then indexed in ascending order.
 *
 * @group is defining the position of a page in a given pair. It can also be
 * seen as the bit position in the cell: page attached to bit 0 belongs to
 * group 0, page attached to bit 1 belongs to group 1, etc.
 *
 * Example:
 * The H27UCG8T2BTR-BC datasheet describes the following pairing scheme:
 *
 *		group-0		group-1
 *
 *  pair-0	page-0		page-4
 *  pair-1	page-1		page-5
 *  pair-2	page-2		page-8
 *  ...
 *  pair-127	page-251	page-255
 *
 *
 * Note that the "group" and "pair" terms were extracted from Samsung and
 * Hynix datasheets, and might be referenced under other names in other
 * datasheets (Micron is describing this concept as "shared pages").
 */
struct mtd_pairing_info {
	int pair;
	int group;
};

/**
 * struct mtd_pairing_scheme - page pairing scheme description
 *
 * @name: name of the pairing scheme (this information is exposed in
 *	  sysfs).
 * @ngroups: number of groups. Should be related to the number of bits
 *	     per cell.
 * @get_info: converts a write-unit (page number within an erase block) into
 *	      mtd_pairing information (pair + group). This function should
 *	      fill the info parameter based on the wunit index or return
 *	      -EINVAL if the wunit parameter is invalid.
 * @get_wunit: converts pairing information into a write-unit (page) number.
 *	       This function should return the wunit index pointed by the
 *	       pairing information described in the info argument. It should
 *	       return -EINVAL, if there's no wunit corresponding to the
 *	       passed pairing information.
 *
 * See mtd_pairing_info documentation for a detailed explanation of the
 * pair and group concepts.
 *
 * The mtd_pairing_scheme structure provides a generic solution to represent
 * NAND page pairing scheme. Instead of exposing two big tables to do the
 * write-unit <-> (pair + group) conversions.
 *
 * MTD users will then be able to query these information by using the
 * mtd_pairing_info_to_wunit() and mtd_wunit_to_pairing_info() helpers.
 *
 * @ngroups is here to help MTD users iterating over all the pages in a
 * given pair. This value can be retrieved by MTD users using the
 * mtd_pairing_groups() helper.
 *
 * Examples are given in the mtd_pairing_info_to_wunit() and
 * mtd_wunit_to_pairing_info() documentation.
 */
struct mtd_pairing_scheme {
	const char *name;
	int ngroups;
	int (*get_info)(const struct mtd_dev_info *mtd, int wunit,
			struct mtd_pairing_info *info);
	int (*get_wunit)(const struct mtd_dev_info *mtd,
			 const struct mtd_pairing_info *info);
};

/**
 * struct mtd_dev_info - information about an MTD device.
 * @mtd_num: MTD device number
 * @major: major number of corresponding character device
 * @minor: minor number of corresponding character device
 * @type: flash type (constants like %MTD_NANDFLASH defined in mtd-abi.h)
 * @type_str: static R/O flash type string
 * @name: device name
 * @size: device size in bytes
 * @eb_cnt: count of eraseblocks
 * @eb_size: eraseblock size
 * @min_io_size: minimum input/output unit size
 * @subpage_size: sub-page size
 * @oob_size: OOB size (zero if the device does not have OOB area)
 * @region_cnt: count of additional erase regions
 * @writable: zero if the device is read-only
 * @bb_allowed: non-zero if the MTD device may have bad eraseblocks
 * @pairing: wunit pairing scheme, if any
 */
struct mtd_dev_info
{
	int mtd_num;
	int major;
	int minor;
	int type;
	const char type_str[MTD_TYPE_MAX + 1];
	const char name[MTD_NAME_MAX + 1];
	long long size;
	int eb_cnt;
	int eb_size;
	int min_io_size;
	int subpage_size;
	int oob_size;
	int region_cnt;
	unsigned int writable:1;
	unsigned int bb_allowed:1;
	const struct mtd_pairing_scheme *pairing;
};

/**
 * libmtd_open - open MTD library.
 *
 * This function initializes and opens the MTD library and returns MTD library
 * descriptor in case of success and %NULL in case of failure. In case of
 * failure, errno contains zero if MTD is not present in the system, or
 * contains the error code if a real error happened.
 */
libmtd_t libmtd_open(void);

/**
 * libmtd_close - close MTD library.
 * @desc: MTD library descriptor
 */
void libmtd_close(libmtd_t desc);

/**
 * mtd_dev_present - check whether a MTD device is present.
 * @desc: MTD library descriptor
 * @mtd_num: MTD device number to check
 *
 * This function returns %1 if MTD device is present and %0 if not.
 */
int mtd_dev_present(libmtd_t desc, int mtd_num);

/**
 * mtd_get_info - get general MTD information.
 * @desc: MTD library descriptor
 * @info: the MTD device information is returned here
 *
 * This function fills the passed @info object with general MTD information and
 * returns %0 in case of success and %-1 in case of failure. If MTD subsystem is
 * not present in the system, errno is set to @ENODEV.
 */
int mtd_get_info(libmtd_t desc, struct mtd_info *info);

/**
 * mtd_get_dev_info - get information about an MTD device.
 * @desc: MTD library descriptor
 * @node: name of the MTD device node
 * @mtd: the MTD device information is returned here
 *
 * This function gets information about MTD device defined by the @node device
 * node file and saves this information in the @mtd object. Returns %0 in case
 * of success and %-1 in case of failure. If MTD subsystem is not present in the
 * system, or the MTD device does not exist, errno is set to @ENODEV.
 */
int mtd_get_dev_info(libmtd_t desc, const char *node, struct mtd_dev_info *mtd);

/**
 * mtd_get_dev_info1 - get information about an MTD device.
 * @desc: MTD library descriptor
 * @mtd_num: MTD device number to fetch information about
 * @mtd: the MTD device information is returned here
 *
 * This function is identical to 'mtd_get_dev_info()' except that it accepts
 * MTD device number, not MTD character device.
 */
int mtd_get_dev_info1(libmtd_t desc, int mtd_num, struct mtd_dev_info *mtd);

/**
 * mtd_lock - lock eraseblocks.
 * @desc: MTD library descriptor
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to lock
 *
 * This function locks eraseblock @eb. Returns %0 in case of success and %-1
 * in case of failure.
 */
int mtd_lock(const struct mtd_dev_info *mtd, int fd, int eb);

/**
 * mtd_unlock - unlock eraseblocks.
 * @desc: MTD library descriptor
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to lock
 *
 * This function unlocks eraseblock @eb. Returns %0 in case of success and %-1
 * in case of failure.
 */
int mtd_unlock(const struct mtd_dev_info *mtd, int fd, int eb);

/**
 * mtd_erase - erase multiple eraseblocks.
 * @desc: MTD library descriptor
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: index of first eraseblock to erase
 * @blocks: the number of eraseblocks to erase
 *
 * This function erases @blocks starting at eraseblock @eb of MTD device
 * described by @fd. Returns %0 in case of success and %-1 in case of failure.
 */
int mtd_erase_multi(libmtd_t desc, const struct mtd_dev_info *mtd,
			int fd, int eb, int blocks);

/**
 * mtd_erase - erase an eraseblock.
 * @desc: MTD library descriptor
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to erase
 *
 * This function erases eraseblock @eb of MTD device described by @fd. Returns
 * %0 in case of success and %-1 in case of failure.
 */
int mtd_erase(libmtd_t desc, const struct mtd_dev_info *mtd, int fd, int eb);

/**
 * mtd_regioninfo - get information about an erase region.
 * @fd: MTD device node file descriptor
 * @regidx: index of region to look up
 * @reginfo: the region information is returned here
 *
 * This function gets information about an erase region defined by the
 * @regidx index and saves this information in the @reginfo object.
 * Returns %0 in case of success and %-1 in case of failure. If the
 * @regidx is not valid or unavailable, errno is set to @ENODEV.
 */
int mtd_regioninfo(int fd, int regidx, struct region_info_user *reginfo);

/**
 * mtd_is_locked - see if the specified eraseblock is locked.
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to check
 *
 * This function checks to see if eraseblock @eb of MTD device described
 * by @fd is locked. Returns %0 if it is unlocked, %1 if it is locked, and
 * %-1 in case of failure. If the ioctl is not supported (support was added in
 * Linux kernel 2.6.36) or this particular device does not support it, errno is
 * set to @ENOTSUPP.
 */
int mtd_is_locked(const struct mtd_dev_info *mtd, int fd, int eb);

/**
 * mtd_torture - torture an eraseblock.
 * @desc: MTD library descriptor
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to torture
 *
 * This function tortures eraseblock @eb. Returns %0 in case of success and %-1
 * in case of failure.
 */
int mtd_torture(libmtd_t desc, const struct mtd_dev_info *mtd, int fd, int eb);

/**
 * mtd_is_bad - check if eraseblock is bad.
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to check
 *
 * This function checks if eraseblock @eb is bad. Returns %0 if not, %1 if yes,
 * and %-1 in case of failure.
 */
int mtd_is_bad(const struct mtd_dev_info *mtd, int fd, int eb);

/**
 * mtd_mark_bad - mark an eraseblock as bad.
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to mark as bad
 *
 * This function marks eraseblock @eb as bad. Returns %0 in case of success and
 * %-1 in case of failure.
 */
int mtd_mark_bad(const struct mtd_dev_info *mtd, int fd, int eb);

/**
 * mtd_read - read data from an MTD device.
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to read from
 * @offs: offset withing the eraseblock to read from
 * @buf: buffer to read data to
 * @len: how many bytes to read
 *
 * This function reads @len bytes of data from eraseblock @eb and offset @offs
 * of the MTD device defined by @mtd and stores the read data at buffer @buf.
 * Returns %0 in case of success and %-1 in case of failure.
 */
int mtd_read(const struct mtd_dev_info *mtd, int fd, int eb, int offs,
	     void *buf, int len);

/**
 * mtd_write - write data to an MTD device.
 * @desc: MTD library descriptor
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @eb: eraseblock to write to
 * @offs: offset withing the eraseblock to write to
 * @data: data buffer to write
 * @len: how many data bytes to write
 * @oob: OOB buffer to write
 * @ooblen: how many OOB bytes to write
 * @mode: write mode (e.g., %MTD_OOB_PLACE, %MTD_OOB_RAW)
 *
 * This function writes @len bytes of data to eraseblock @eb and offset @offs
 * of the MTD device defined by @mtd. Returns %0 in case of success and %-1 in
 * case of failure.
 *
 * Can only write to a single page at a time if writing to OOB.
 */
int mtd_write(libmtd_t desc, const struct mtd_dev_info *mtd, int fd, int eb,
	      int offs, void *data, int len, void *oob, int ooblen,
	      uint8_t mode);

/**
 * mtd_read_oob - read out-of-band area.
 * @desc: MTD library descriptor
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @start: page-aligned start address
 * @length: number of OOB bytes to read
 * @data: read buffer
 *
 * This function reads @length OOB bytes starting from address @start on
 * MTD device described by @fd. The address is specified as page byte offset
 * from the beginning of the MTD device. This function returns %0 in case of
 * success and %-1 in case of failure.
 */
int mtd_read_oob(libmtd_t desc, const struct mtd_dev_info *mtd, int fd,
		 uint64_t start, uint64_t length, void *data);

/**
 * mtd_write_oob - write out-of-band area.
 * @desc: MTD library descriptor
 * @mtd: MTD device description object
 * @fd: MTD device node file descriptor
 * @start: page-aligned start address
 * @length: number of OOB bytes to write
 * @data: write buffer
 *
 * This function writes @length OOB bytes starting from address @start on
 * MTD device described by @fd. The address is specified as page byte offset
 * from the beginning of the MTD device. Returns %0 in case of success and %-1
 * in case of failure.
 */
int mtd_write_oob(libmtd_t desc, const struct mtd_dev_info *mtd, int fd,
		  uint64_t start, uint64_t length, void *data);

/**
 * mtd_probe_node - test MTD node.
 * @desc: MTD library descriptor
 * @node: the node to test
 *
 * This function tests whether @node is an MTD device node and returns %1 if it
 * is, and %-1 if it is not (errno is %ENODEV in this case) or if an error
 * occurred.
 */
int mtd_probe_node(libmtd_t desc, const char *node);

/**
 * mtd_wunit_to_pairing_info - get pairing information of a wunit
 * @mtd: pointer to new MTD device info structure
 * @wunit: write unit we are interested in
 * @info: returned pairing information
 *
 * Retrieve pairing information associated to the wunit.
 * This is mainly useful when dealing with MLC/TLC NANDs where pages can be
 * paired together, and where programming a page may influence the page it is
 * paired with.
 * The notion of page is replaced by the term wunit (write-unit) to stay
 * consistent with the ->writesize field.
 *
 * The @wunit argument can be extracted from an absolute offset using
 * mtd_offset_to_wunit(). @info is filled with the pairing information attached
 * to @wunit.
 *
 * From the pairing info the MTD user can find all the wunits paired with
 * @wunit using the following loop:
 *
 * for (i = 0; i < mtd_pairing_groups(mtd); i++) {
 *	info.pair = i;
 *	mtd_pairing_info_to_wunit(mtd, &info);
 *	...
 * }
 */
int mtd_wunit_to_pairing_info(const struct mtd_dev_info *mtd, int wunit,
			      struct mtd_pairing_info *info);

/**
 * mtd_wunit_to_pairing_info - get wunit from pairing information
 * @mtd: pointer to new MTD device info structure
 * @info: pairing information struct
 *
 * Returns a positive number representing the wunit associated to the info
 * struct, or a negative error code.
 *
 * This is the reverse of mtd_wunit_to_pairing_info(), and can help one to
 * iterate over all wunits of a given pair (see mtd_wunit_to_pairing_info()
 * doc).
 *
 * It can also be used to only program the first page of each pair (i.e.
 * page attached to group 0), which allows one to use an MLC NAND in
 * software-emulated SLC mode:
 *
 * info.group = 0;
 * npairs = mtd_wunit_per_eb(mtd) / mtd_pairing_groups(mtd);
 * for (info.pair = 0; info.pair < npairs; info.pair++) {
 *	wunit = mtd_pairing_info_to_wunit(mtd, &info);
 *	mtd_write(mtd, mtd_wunit_to_offset(mtd, blkoffs, wunit),
 *		  mtd->writesize, &retlen, buf + (i * mtd->writesize));
 * }
 */
int mtd_pairing_info_to_wunit(const struct mtd_dev_info *mtd,
			      const struct mtd_pairing_info *info);

/**
 * mtd_pairing_groups - get the number of pairing groups
 * @mtd: pointer to new MTD device info structure
 *
 * Returns the number of pairing groups.
 *
 * This number is usually equal to the number of bits exposed by a single
 * cell, and can be used in conjunction with mtd_pairing_info_to_wunit()
 * to iterate over all pages of a given pair.
 */
int mtd_pairing_groups(const struct mtd_dev_info *mtd);

/**
 * mtd_get_pairing_scheme - get a pairing scheme object from its name
 * @name: pairing scheme name
 *
 * Returns the pairing scheme descriptor or NULL if it does not exist.
 */
const struct mtd_pairing_scheme *mtd_get_pairing_scheme(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMTD_H__ */
