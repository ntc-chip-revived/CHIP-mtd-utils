/*
 * Copyright (c) International Business Machines Corp., 2006
 * Copyright (C) 2008 Nokia Corporation
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
 */

/*
 * Generating UBI images.
 *
 * Authors: Oliver Lohmann
 *          Artem Bityutskiy
 */

#define PROGRAM_NAME "libubigen"

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include <mtd/ubi-media.h>
#include <mtd_swab.h>
#include <libubigen.h>
#include <libubi.h>
#include <crc32.h>
#include "common.h"

void ubigen_info_init(struct ubigen_info *ui, int peb_size, int min_io_size,
		      int subpage_size, int vid_hdr_offs, int ubi_ver,
		      uint32_t image_seq,
		      const struct mtd_pairing_scheme *pairing)
{
	if (!vid_hdr_offs) {
		vid_hdr_offs = UBI_EC_HDR_SIZE + subpage_size - 1;
		vid_hdr_offs /= subpage_size;
		vid_hdr_offs *= subpage_size;
	}

	ui->peb_size = peb_size;
	ui->min_io_size = min_io_size;
	ui->vid_hdr_offs = vid_hdr_offs;
	ui->data_offs = vid_hdr_offs + UBI_VID_HDR_SIZE + min_io_size - 1;
	ui->data_offs /= min_io_size;
	ui->data_offs *= min_io_size;
	ui->leb_size = peb_size - ui->data_offs;
	ui->ubi_ver = ubi_ver;
	ui->image_seq = image_seq;

	ui->max_volumes = ui->leb_size / UBI_VTBL_RECORD_SIZE;
	if (ui->max_volumes > UBI_MAX_VOLUMES)
		ui->max_volumes = UBI_MAX_VOLUMES;
	ui->vtbl_size = ui->max_volumes * UBI_VTBL_RECORD_SIZE;

	ui->mtd.eb_size = ui->peb_size;
	ui->mtd.min_io_size = ui->min_io_size;
	ui->mtd.pairing = pairing;

	if (pairing) {
		ui->slc_leb_size = (peb_size / pairing->ngroups) -
				   ui->data_offs;
		ui->max_lebs_per_peb = pairing->ngroups;
	} else {
		ui->slc_leb_size = ui->leb_size;
		ui->max_lebs_per_peb = 1;
	}
}

struct ubi_vtbl_record *ubigen_create_empty_vtbl(const struct ubigen_info *ui)
{
	struct ubi_vtbl_record *vtbl;
	int i;

	vtbl = calloc(1, ui->vtbl_size);
	if (!vtbl) {
		sys_errmsg("cannot allocate %d bytes of memory", ui->vtbl_size);
		return NULL;
	}

	for (i = 0; i < ui->max_volumes; i++) {
		uint32_t crc = mtd_crc32(UBI_CRC32_INIT, &vtbl[i],
				     UBI_VTBL_RECORD_SIZE_CRC);
		vtbl[i].crc = cpu_to_be32(crc);
	}

	return vtbl;
}

int ubigen_add_volume(const struct ubigen_info *ui,
		      const struct ubigen_vol_info *vi,
		      struct ubi_vtbl_record *vtbl)
{
	struct ubi_vtbl_record *vtbl_rec = &vtbl[vi->id];
	int leb_size, npebs, nlebs;
	uint32_t tmp;

	if (vi->id >= ui->max_volumes) {
		errmsg("too high volume id %d, max. volumes is %d",
		       vi->id, ui->max_volumes);
		errno = EINVAL;
		return -1;
	}

	if (vi->alignment >= ui->leb_size) {
		errmsg("too large alignment %d, max is %d (LEB size)",
		       vi->alignment, ui->leb_size);
		errno = EINVAL;
		return -1;
	}

	memset(vtbl_rec, 0, sizeof(struct ubi_vtbl_record));
	if (vi->mode == UBI_VID_MODE_SLC || vi->mode == UBI_VID_MODE_MLC_SAFE)
		leb_size = ui->slc_leb_size;
	else
		leb_size = ui->leb_size;

	nlebs = (uint64_t)(vi->bytes + leb_size - 1) / leb_size;
	npebs = ubi_lebs_to_pebs(ui->max_lebs_per_peb, vi->mode,
				 vi->slc_ratio, nlebs);
	if (npebs < 0) {
		errmsg("could not calculate the required number of PEBs");
		errno = npebs;
		return -1;
	}

	tmp = (vi->bytes + ui->leb_size - 1) / ui->leb_size;
	vtbl_rec->reserved_pebs = cpu_to_be32(npebs);
	vtbl_rec->alignment = cpu_to_be32(vi->alignment);
	vtbl_rec->vol_type = vi->type;
	vtbl_rec->vol_mode = vi->mode;
	tmp = ui->leb_size % vi->alignment;
	vtbl_rec->data_pad = cpu_to_be32(tmp);
	vtbl_rec->flags = vi->flags;

	if (vi->mode == UBI_VID_MODE_MLC_SAFE) {
		vtbl->slc_ratio = vi->slc_ratio;
		vtbl_rec->reserved_lebs = cpu_to_be32(nlebs);
	}

	memcpy(vtbl_rec->name, vi->name, vi->name_len);
	vtbl_rec->name[vi->name_len] = '\0';
	vtbl_rec->name_len = cpu_to_be16(vi->name_len);

	tmp = mtd_crc32(UBI_CRC32_INIT, vtbl_rec, UBI_VTBL_RECORD_SIZE_CRC);
	vtbl_rec->crc =	 cpu_to_be32(tmp);
	return 0;
}

void ubigen_init_ec_hdr(const struct ubigen_info *ui,
		        struct ubi_ec_hdr *hdr, long long ec)
{
	uint32_t crc;

	memset(hdr, 0, sizeof(struct ubi_ec_hdr));

	hdr->magic = cpu_to_be32(UBI_EC_HDR_MAGIC);
	hdr->version = ui->ubi_ver;
	hdr->ec = cpu_to_be64(ec);
	hdr->vid_hdr_offset = cpu_to_be32(ui->vid_hdr_offs);
	hdr->data_offset = cpu_to_be32(ui->data_offs);
	hdr->image_seq = cpu_to_be32(ui->image_seq);

	crc = mtd_crc32(UBI_CRC32_INIT, hdr, UBI_EC_HDR_SIZE_CRC);
	hdr->hdr_crc = cpu_to_be32(crc);
}

void ubigen_init_vid_hdr(const struct ubigen_info *ui,
			 const struct ubigen_vol_info *vi,
			 struct ubi_vid_hdr *hdr, int lnum,
			 int lpos, const void *data, int data_size)
{
	uint32_t crc;

	memset(hdr, 0, sizeof(struct ubi_vid_hdr));

	hdr->magic = cpu_to_be32(UBI_VID_HDR_MAGIC);
	hdr->version = ui->ubi_ver;
	hdr->vol_type = vi->type;
	hdr->vol_mode = vi->mode;
	hdr->vol_id = cpu_to_be32(vi->id);
	hdr->lnum = cpu_to_be32(lnum);
	if (vi->mode == UBI_VID_MODE_MLC_SAFE)
		hdr->lpos = lpos;
	hdr->data_pad = cpu_to_be32(vi->data_pad);
	hdr->compat = vi->compat;

	if (vi->type == UBI_VID_STATIC) {
		hdr->data_size = cpu_to_be32(data_size);
		hdr->used_ebs = cpu_to_be32(vi->used_ebs);
		crc = mtd_crc32(UBI_CRC32_INIT, data, data_size);
		hdr->data_crc = cpu_to_be32(crc);
	}

	crc = mtd_crc32(UBI_CRC32_INIT, hdr, UBI_VID_HDR_SIZE_CRC);
	hdr->hdr_crc = cpu_to_be32(crc);
}

static void ubigen_init_dummy_vid_hdr(const struct ubigen_info *ui,
				      const struct ubigen_vol_info *vi,
				      struct ubi_vid_hdr *hdr)
{
	uint32_t crc;

	memset(hdr, 0, sizeof(struct ubi_vid_hdr));

	hdr->magic = cpu_to_be32(UBI_VID_HDR_MAGIC);
	hdr->version = ui->ubi_ver;
	hdr->vol_type = vi->type;
	hdr->vol_mode = vi->mode;
	hdr->lpos = UBI_VID_LPOS_CONSOLIDATED;
	crc = mtd_crc32(UBI_CRC32_INIT, hdr, UBI_VID_HDR_SIZE_CRC);
	hdr->hdr_crc = cpu_to_be32(crc);

}

void ubigen_layout_vid_and_data(const struct ubigen_info *ui,
				const struct ubigen_vol_info *vi,
				int lnum, const void *inbuf, void *outbuf,
				int len)
{
	const struct mtd_dev_info *mtd = &ui->mtd;
	struct ubi_vid_hdr *vid_hdr;
	int offset, nlebs, i;

	memset(outbuf + ui->vid_hdr_offs, 0x00,
	       ui->data_offs - ui->vid_hdr_offs);
	vid_hdr = outbuf + ui->vid_hdr_offs;

	if (vi->mode == UBI_VID_MODE_MLC_SAFE)
		ubigen_init_dummy_vid_hdr(ui, vi, vid_hdr);
	else
		ubigen_init_vid_hdr(ui, vi, vid_hdr, lnum, 0, inbuf, len);

	memset(outbuf + ui->data_offs, 0xFF, ui->peb_size - ui->data_offs);

	if (vi->mode == UBI_VID_MODE_SLC && ui->max_lebs_per_peb > 1) {
		struct mtd_pairing_info info;
		int npairs, nwunits;

		nwunits = ui->peb_size / ui->min_io_size;
		npairs = nwunits / mtd_pairing_groups(mtd);

		info.group = 0;
		info.pair = (ui->data_offs + ui->min_io_size - 1) /
			    ui->min_io_size;

		for (; info.pair < npairs && len; info.pair++) {
			int wunit, wsize = ui->min_io_size;

			wunit = mtd_pairing_info_to_wunit(mtd, &info);

			if (wsize > len)
				wsize = len;

			offset = wunit * ui->min_io_size;

			memcpy(outbuf + offset, inbuf, wsize);
			inbuf += wsize;
			len -= wsize;
		}
	} else {
		memcpy(outbuf + ui->data_offs, inbuf, len);
	}

	if (vi->mode != UBI_VID_MODE_MLC_SAFE)
		return;

	offset = ui->data_offs + (ui->slc_leb_size * ui->max_lebs_per_peb);
	memset(outbuf + offset, 0, ui->peb_size - offset);
	vid_hdr = outbuf + ui->peb_size - ui->min_io_size;

	nlebs = (len + ui->slc_leb_size - 1) / ui->slc_leb_size;

	for (i = 0; i < nlebs; i++)
		ubigen_init_vid_hdr(ui, vi, vid_hdr, lnum, i, inbuf, len);

	for (; i < ui->max_lebs_per_peb; i++)
		ubigen_init_vid_hdr(ui, vi, vid_hdr, 0, UBI_VID_LPOS_INVALID,
				    inbuf, len);
}

int ubigen_write_volume(const struct ubigen_info *ui,
			const struct ubigen_vol_info *vi, long long ec,
			long long bytes, int in, int out)
{
	int len = vi->usable_leb_size, rd, lnum = 0;
	char *inbuf, *outbuf;

	if (vi->id >= ui->max_volumes) {
		errmsg("too high volume id %d, max. volumes is %d",
		       vi->id, ui->max_volumes);
		errno = EINVAL;
		return -1;
	}

	if (vi->alignment >= ui->leb_size) {
		errmsg("too large alignment %d, max is %d (LEB size)",
		       vi->alignment, ui->leb_size);
		errno = EINVAL;
		return -1;
	}

	inbuf = malloc(ui->peb_size);
	if (!inbuf)
		return sys_errmsg("cannot allocate %d bytes of memory",
				  ui->leb_size);
	outbuf = malloc(ui->peb_size);
	if (!outbuf) {
		sys_errmsg("cannot allocate %d bytes of memory", ui->peb_size);
		goto out_free;
	}

	memset(outbuf, 0xFF, ui->data_offs);
	ubigen_init_ec_hdr(ui, (struct ubi_ec_hdr *)outbuf, ec);

	if (vi->mode == UBI_VID_MODE_MLC_SAFE)
		len *= ui->max_lebs_per_peb;

	while (bytes) {
		int l;

		if (bytes < len)
			len = bytes;
		bytes -= len;

		l = len;
		do {
			rd = read(in, inbuf + len - l, l);
			if (rd != l) {
				sys_errmsg("cannot read %d bytes from the input file", l);
				goto out_free1;
			}

			l -= rd;
		} while (l);

		ubigen_layout_vid_and_data(ui, vi, lnum, inbuf, outbuf, len);

		if (write(out, outbuf, ui->peb_size) != ui->peb_size) {
			sys_errmsg("cannot write %d bytes to the output file", ui->peb_size);
			goto out_free1;
		}

		lnum += ui->max_lebs_per_peb;
	}

	free(outbuf);
	free(inbuf);
	return 0;

out_free1:
	free(outbuf);
out_free:
	free(inbuf);
	return -1;
}

int ubigen_write_layout_vol(const struct ubigen_info *ui, int peb1, int peb2,
			    long long ec1, long long ec2,
			    struct ubi_vtbl_record *vtbl, int fd)
{
	int ret;
	struct ubigen_vol_info vi;
	char *outbuf;
	struct ubi_vid_hdr *vid_hdr;
	off_t seek;

	vi.bytes = ui->leb_size * UBI_LAYOUT_VOLUME_EBS;
	vi.id = UBI_LAYOUT_VOLUME_ID;
	vi.alignment = UBI_LAYOUT_VOLUME_ALIGN;
	vi.data_pad = ui->leb_size % UBI_LAYOUT_VOLUME_ALIGN;
	vi.usable_leb_size = ui->leb_size - vi.data_pad;
	vi.data_pad = ui->leb_size - vi.usable_leb_size;
	vi.type = UBI_LAYOUT_VOLUME_TYPE;
	vi.name = UBI_LAYOUT_VOLUME_NAME;
	vi.name_len = strlen(UBI_LAYOUT_VOLUME_NAME);
	vi.compat = UBI_LAYOUT_VOLUME_COMPAT;

	outbuf = malloc(ui->peb_size);
	if (!outbuf)
		return sys_errmsg("failed to allocate %d bytes",
				  ui->peb_size);

	memset(outbuf, 0xFF, ui->data_offs);
	vid_hdr = (struct ubi_vid_hdr *)(&outbuf[ui->vid_hdr_offs]);
	memcpy(outbuf + ui->data_offs, vtbl, ui->vtbl_size);
	memset(outbuf + ui->data_offs + ui->vtbl_size, 0xFF,
	       ui->peb_size - ui->data_offs - ui->vtbl_size);

	seek = (off_t) peb1 * ui->peb_size;
	if (lseek(fd, seek, SEEK_SET) != seek) {
		sys_errmsg("cannot seek output file");
		goto out_free;
	}

	ubigen_init_ec_hdr(ui, (struct ubi_ec_hdr *)outbuf, ec1);
	ubigen_init_vid_hdr(ui, &vi, vid_hdr, 0, 0, NULL, 0);
	ret = write(fd, outbuf, ui->peb_size);
	if (ret != ui->peb_size) {
		sys_errmsg("cannot write %d bytes", ui->peb_size);
		goto out_free;
	}

	seek = (off_t) peb2 * ui->peb_size;
	if (lseek(fd, seek, SEEK_SET) != seek) {
		sys_errmsg("cannot seek output file");
		goto out_free;
	}
	ubigen_init_ec_hdr(ui, (struct ubi_ec_hdr *)outbuf, ec2);
	ubigen_init_vid_hdr(ui, &vi, vid_hdr, 1, 0, NULL, 0);
	ret = write(fd, outbuf, ui->peb_size);
	if (ret != ui->peb_size) {
		sys_errmsg("cannot write %d bytes", ui->peb_size);
		goto out_free;
	}

	free(outbuf);
	return 0;

out_free:
	free(outbuf);
	return -1;
}
