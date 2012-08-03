
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
	fprintf(stderr, "Usage: %s [-f] [-v] cachename old_ssd_partition new_ssd_partition\n", pname);
	fprintf(stderr, "       (ex: %s vicepba /dev/sdj2)\n", pname);
	fprintf(stderr, "       -f: force\n");
	fprintf(stderr, "       -v: verbose\n");
	exit(1);
}

static void
check_sure(void)
{
        int input;

        fprintf(stderr, "Are you sure you want to proceed ? (y/n): ");
        input = getchar();
        if (input != 'y') {
                fprintf(stderr, "Exiting FlashCache replacement.\n");
                exit(1);
        }
	while ((input = getchar()) != '\n' && input != EOF);
}

main(int argc, char **argv)
{
	int ssd_fd, cache_fd, c, i;
	char *cachename, *old_ssd_devname, *new_ssd_devname;
	char flashcache_devname[256], cmd[256];
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
        old_ssd_devname = argv[optind++];
	if (optind == argc)
		usage(pname);
	new_ssd_devname = argv[optind++];

	sprintf(cmd, "/sbin/dmsetup table %s|grep -q %s", cachename, old_ssd_devname);
        if (system(cmd)) {
                fprintf(stderr, "%s seems not to be part of %s! Exiting.\n", old_ssd_devname, cachename);
                exit(1);
        }

	/* get the old underlying SSD device */
	i = strcspn(old_ssd_devname, "0123456789");	
	old_ssd_devname[i] = '\0';
	{
		FILE *fp;
  		char line[256];
		char *c;
		int found=0, rc=0;

		sprintf(cmd, "/usr/bin/lsscsi | grep '%s[[:space:]]*$'", old_ssd_devname);
  		fp = popen(cmd, "r");
  		if (fp == NULL) {
    			fprintf(stderr, "Failed to run command\n" );
    			exit;
  		}
  		while (fgets(line, sizeof(line)-1, fp) != NULL) {
			printf("Found device:\n\n\t%s", line);
    			found++;
			if (found>1) {
				fprintf(stderr, "\nMultiple devices found! This is a bug! Exiting.");
				exit(1);
			}
			i = strcspn(line, " ");
			line[i] = '\0'; 			
                	while (c = strpbrk(line, ":[]")) {
                        	*c = ' ';
                	}
			sprintf(cmd, "echo \"scsi remove-single-device%s\" > /proc/scsi/scsi", line);
			printf("\nPropose to remove it with:\n\n\t%s\n\n", cmd);
			check_sure();
			if (0 != system(cmd)) {
				fprintf(stderr, "Unable to remove the device! Exiting.\n");
                                exit(1);
			}

		}
  		pclose(fp);
	}

	sprintf(flashcache_devname, "/dev/mapper/%s", cachename);	
	printf("\nReplacing SSD in cache %s (%s) with %s\n", 
       		cachename, flashcache_devname, new_ssd_devname);
	check_sure();

	/* check for a writethrough superblock */
        ssd_fd = open(new_ssd_devname, O_RDONLY);
        if (ssd_fd < 0) {
                fprintf(stderr, "Failed to open %s. Exiting.\n", new_ssd_devname);
                exit(1);
        } else {
		long off=0;
		int i;
		sector_t sb_sectors;
			
		sb_sectors =(sizeof(struct flash_superblock))/512 + 1;
		off = lseek(ssd_fd, sb_sectors * -512, SEEK_END);
		if (read(ssd_fd, buf, 512) < 0) {
                	fprintf(stderr, "Cannot read flashcache write-through superblock %s. Exiting.\n",
                               	 new_ssd_devname);
                       	exit(1);
         	}
		fprintf(stderr, "Current flashcache name is %s (from superblock).\n", sb->cache_devname);
         	if (!force && (strncmp(sb->cache_devname, cachename, DEV_PATHLEN) != 0)) {
                	fprintf(stderr, "Valid Flashcache already exists on %s: \"%s\" (differs from wanted \"%s\"). Exiting.\n",
                       		new_ssd_devname, sb->cache_devname, cachename);
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
                        pname, new_ssd_devname);
                exit(1);
        }
	close(ssd_fd);

        strncpy(sb->cache_devname, new_ssd_devname, DEV_PATHLEN);
	sb->cache_devsize = cache_devsize;

	/* replace SSDs */
	printf("Replacing SSDs in the kernel module, please be patient (~1 min) ...\n");
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
