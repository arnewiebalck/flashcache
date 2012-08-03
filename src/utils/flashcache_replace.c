
/*
 * Copyright (c) 2010, Facebook, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * Neither the name Facebook nor the names of its contributors may be used to
 * endorse or promote products derived from this software without specific
 * prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <flashcache.h>
#include <flashcache_ioctl.h>

char *pname;
int verbose = 0;
int force = 0;
char buf[512];

void
usage(char *pname)
{
	fprintf(stderr, "Usage: %s [-f] [-v] cachename flashcache_device new_ssd_partition\n", pname);
	fprintf(stderr, "       (ex: %s vicepba /dev/mapper/vicepba /dev/sdj2)\n", pname);
	fprintf(stderr, "       -f: force\n");
	fprintf(stderr, "       -v: verbose\n");
	exit(1);
}

static void
check_sure(void)
{
        char input;

        fprintf(stderr, "Are you sure you want to proceed ? (y/n): ");
        scanf("%c", &input);
        if (input != 'y') {
                fprintf(stderr, "Exiting FlashCache replacement.\n");
                exit(1);
        }
}

main(int argc, char **argv)
{
	int ssd_fd, cache_fd, c;
	char *cachename, *flashcache_devname, *ssd_devname;
	struct flash_superblock *sb = (struct flash_superblock *)buf;
	sector_t cache_devsize=0;

	pname = argv[0];
	while ((c = getopt(argc, argv, "fv")) != -1) {
		switch (c) {
		case 'v':
			verbose = 1;
                        break;		
		case 'f':
                        force = 1;
                        break;   	
		case '?':
			usage(pname);
		}
	}
	if (optind == argc)
                usage(pname);
        cachename = argv[optind++];
       	if (optind == argc)
                usage(pname);
	flashcache_devname = argv[optind++];
	if (optind == argc)
		usage(pname);
	ssd_devname = argv[optind++];
	printf("Replacing SSD in cache %s (%s) with %s\n", 
       		cachename, flashcache_devname, ssd_devname);
	check_sure();

	/* check for a writethrough superblock */
        ssd_fd = open(ssd_devname, O_RDONLY);
        if (ssd_fd < 0) {
                fprintf(stderr, "Failed to open %s. Exiting.\n", ssd_devname);
                exit(1);
        } else {
		long off=0;
		int i;
		sector_t sb_sectors;
			
		sb_sectors =(sizeof(struct flash_superblock))/512 + 1;
		off = lseek(ssd_fd, sb_sectors * -512, SEEK_END);
		if (read(ssd_fd, buf, 512) < 0) {
                	fprintf(stderr, "Cannot read flashcache write-through superblock %s. Exiting.\n",
                               	 ssd_devname);
                       	exit(1);
         	}
		fprintf(stderr, "Current flashcache name is %s (from superblock).\n", sb->cache_devname);
         	if (!force && (strncmp(sb->cache_devname, cachename, DEV_PATHLEN) != 0)) {
                	fprintf(stderr, "Valid Flashcache already exists on %s: \"%s\" (differs from wanted \"%s\"). Exiting.\n",
                       		ssd_devname, sb->cache_devname, cachename);
               		exit(1);
         	}
	}

        cache_fd = open(flashcache_devname, O_RDONLY);
        if (cache_fd < 1) {
                fprintf(stderr, "Failed to open %s. Exiting.\n", flashcache_devname);
		exit(-1);
	}
 	if (ioctl(ssd_fd, BLKGETSIZE, &cache_devsize) < 0) {
                fprintf(stderr, "%s: Cannot get cache size %s\n",
                        pname, ssd_devname);
                exit(1);
        }
	close(ssd_fd);

        strncpy(sb->cache_devname, ssd_devname, DEV_PATHLEN);
	sb->cache_devsize = cache_devsize;

	/* replace SSDs */
	printf("Replacing SSDs in the kernel module ...\n");
        if (ioctl(cache_fd, FLASHCACHEREPLACECACHEDEV, sb) < 0) {
		fprintf(stderr, "Replacement ioctl failed. Exiting.");
                close(cache_fd);
                exit(-1);
        } else {	
		if (verbose) {
			fprintf(stderr, "Replacement ioctl succeeded.\n");
		}
	}	
}
