#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <unistd.h>
#include <string.h>
#include "zlib_handler.h"

unsigned char *
write_cb(unsigned char *buf, int size, void *arg)
{
	struct writer_args *writer_args = arg;

	writer_args->sent += write(writer_args->fd, buf, size);

	return buf;
}

int
deflate_caller(int sourcefd, inflated_handler inflated_handler, void *arg) {
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];
	unsigned have;
	z_stream strm;
	int ret;
	
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit(&strm);
	if (ret != Z_OK)
		return ret;

	do {
		strm.avail_in = read(sourcefd, in, CHUNK);
		if (strm.avail_in == -1) {
			(void)inflateEnd(&strm);
			perror("read from source file");
			return Z_ERRNO;
		}
		if (strm.avail_in == 0)
			break;
		strm.next_in = in;

		do {
			strm.avail_out = CHUNK;
			strm.next_out = out;
			ret = inflate(&strm, Z_NO_FLUSH);
			switch(ret) {
			case Z_NEED_DICT:
				ret = Z_DATA_ERROR;
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void)inflateEnd(&strm);
				return ret;
			}
			have = CHUNK - strm.avail_out;

			// Return value of 0 code means exit
			if (inflated_handler(out, have, arg) != NULL)
				goto end_inflation;

		} while(strm.avail_out == 0);
	} while(ret != Z_STREAM_END);

end_inflation:
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

int
zlib_deliver_loose_object_content(unsigned char *buf, int size, void *data)
{
	return 0;
}