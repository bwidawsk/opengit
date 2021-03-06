/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Farhan Khan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <fetch.h>
#include "lib/zlib-handler.h"
#include "lib/common.h"
#include "lib/ini.h"
#include "lib/pack.h"
#include "clone.h"

static struct option long_options[] =
{
	{NULL, 0, NULL, 0}
};

void
index_pack_usage(int type)
{
	fprintf(stderr, "usage: git index-pack [-v] [-o <index-file>] [--keep | --keep=<msg>] [--verify] [--strict] (<pack-file> | --stdin [--fix-thin] [<pack-file>])\n");
	exit(128);
}

int
index_pack_main(int argc, char *argv[])
{
	int ret = 0;
	int ch;
	int q = 0;

	argc--; argv++;

	while((ch = getopt_long(argc, argv, "", long_options, NULL)) != -1)
		switch(ch) {
		case 0:
			break;
		case 1:
			break;
		default:
			printf("Currently not implemented\n");
			return -1;
		}
	argc = argc - q;
	argv = argv + q;

	if (strncmp(".pack", argv[1] + strlen(argv[1])-5, 5) != 0) {
		fprintf(stderr, "fatal: packfile name '%s' does not end with '.pack'\n", argv[1]);
		exit(128);
	}

	int packfd;
	struct packfilehdr packfilehdr;
	int offset;
	struct objectinfo objectinfo;
	int x;

	packfd = open(argv[1], O_RDONLY);
	if (packfd == -1) {
		fprintf(stderr, "fatal: cannot open packfile '%s'\n", argv[1]);
		exit(128);
	}
	pack_parse_header(packfd, &packfilehdr);
	offset = 4 * 3; 

	struct object_index_entry *object_index_entry;
	struct index_generate_arg index_generate_arg;
	char hdr[32];
	int hdrlen;

	object_index_entry = malloc(sizeof(struct object_index_entry) * packfilehdr.nobjects);

	/* Same as GNU git's parse_pack_objects, first pass */
	for(x = 0; x < packfilehdr.nobjects; x++) {

		lseek(packfd, offset, SEEK_SET);
		pack_object_header(packfd, offset, &objectinfo);
		//bzero(object_index_entry[x].sha, 41);

		switch(objectinfo.ptype) {
		case OBJ_REF_DELTA:
			fprintf(stderr, "OBJ_REF_DELTA: currently not implemented. Exiting.\n"); exit(0);
			offset += objectinfo.used;
			lseek(packfd, offset, SEEK_SET);
			lseek(packfd, 2, SEEK_CUR);
			read(packfd, object_index_entry[x].digest, 20);
			//object_index_entry[x].sha[40] = '\0';
			offset += 22; /* 20 bytes + 2 for the header */
			break;
		case OBJ_OFS_DELTA:
			SHA1_Init(&index_generate_arg.shactx);
			pack_delta_content(packfd, &objectinfo);
			hdrlen = sprintf(hdr, "%s %lu", object_name[objectinfo.ftype],
			    objectinfo.isize) + 1;
			SHA1_Update(&index_generate_arg.shactx, hdr, hdrlen);
			SHA1_Update(&index_generate_arg.shactx, objectinfo.data, objectinfo.isize);
			SHA1_Final(object_index_entry[x].digest,
			    &index_generate_arg.shactx);
			// The next two are allocated in pack_delta_content
			free(objectinfo.data);
			free(objectinfo.deltas);

			offset = objectinfo.offset + objectinfo.used + objectinfo.ofshdrsize + objectinfo.deflated_size;

			break;
		case OBJ_COMMIT:
		case OBJ_TREE:
		case OBJ_BLOB:
		case OBJ_TAG:
		default:
			offset += objectinfo.used;
			lseek(packfd, offset, SEEK_SET);
			index_generate_arg.bytes = 0;
			SHA1_Init(&index_generate_arg.shactx);

			hdrlen = sprintf(hdr, "%s %lu", object_name[objectinfo.ftype],
			    objectinfo.psize) + 1; // XXX This should be isize, not psize
			// XXX this should be SHA1_Update, not SHA_Update
			SHA1_Update(&index_generate_arg.shactx, hdr, hdrlen);
			deflate_caller(packfd, pack_get_index_bytes_cb, &index_generate_arg);
			object_index_entry[x].offset = index_generate_arg.bytes;

			SHA1_Final(object_index_entry[x].digest, &index_generate_arg.shactx);
			offset += index_generate_arg.bytes;
			break;
		}
//		for(q=0;q<20;q++)
//			printf("%02x", object_index_entry[x].digest[q]);
//		printf("\n");
	}

	close(packfd);
	// Now the idx file

	qsort(object_index_entry, packfilehdr.nobjects, sizeof(struct object_index_entry), sortindexentry);

	int idxfd;
	int hashnum;
	int reversed;
	idxfd = open("packout.idx", O_WRONLY | O_CREAT, 0660);
	if (idxfd == -1) {
		fprintf(stderr, "Unable to open packout.idx for writing\n");
		exit(idxfd);
	}

	// Write the header 
	write(idxfd, "\377tOc", 4);		// Header
	write(idxfd, "\x00\x00\x00\x02", 4);	// Version

	/* Writing the Fan Table */

	/* Writing hash count */
	hashnum = 0;
	for(x = 0; x < 256 ; x++) {
		while(object_index_entry[hashnum].digest[0] == x)
			hashnum++;
		reversed = htonl(hashnum);
		write(idxfd, &reversed, 4);
	}

	/* Writing hashes */
	for(x = 0; x < packfilehdr.nobjects; x++)
		write(idxfd, object_index_entry[x].digest, 20);

	/* Write the SHA1 checksum of the corresponding packfile? */
	for(x = 0; x < packfilehdr.nobjects; x++) {
	}

	close(idxfd);

	free(object_index_entry);
	return (ret);
}

