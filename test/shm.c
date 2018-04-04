/*
 * Copyright (c) 2016 Stanley Uche Godfrey
 * Copyright (c) 2017-2018 Jonathan Anderson
 * All rights reserved.
 *
 * This software was developed at Memorial University under the
 * NSERC Discovery program (RGPIN-2015-06048).
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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

/* RUN: %cc -D PARENT %cflags -D TEST_DATA_DIR="\"%p/Inputs\"" %s %ldflags -o %t.parent
 * RUN: %cc -D CHILD %cflags -D TEST_DATA_DIR="\"%p/Inputs\"" %s %ldflags -o %t.child
 * RUN: %t.parent %t.child > %t.out
 * RUN: %filecheck %s -input-file %t.out
 */

#include <sys/mman.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "libpreopen.h"

#define TEST_DIR(name) \
	TEST_DATA_DIR name

#ifdef PARENT

char **environ;

int main(int argc, char *argv[]){
	char buffer[20];
	struct po_map *map;
	int len, shmfd;
	int foo, wibble;

	// CHECK: {{.*}}.parent
	printf("----------------------------------------"
		"----------------------------------------\n");
	printf("%s\n", argv[0]);
	printf("----------------------------------------"
		"----------------------------------------\n");

	if (argc < 2) {
		errx(-1, "Usage: %s <binary to exec as child>", argv[0]);
	}

	map = po_map_get();

	// CHECK: saving [[FOO:[0-9]+]] as "foo"
	foo = openat(AT_FDCWD, TEST_DIR("/foo"), O_RDONLY);
	assert(foo >= 0);
	printf("saving %d as \"foo\"\n", foo);
	po_add(map, "foo", foo);

	// CHECK: pre-opened [[WIBBLE:[0-9]+]] as "[[WIBBLE_PATH:.*]]"
	wibble = po_preopen(map, TEST_DIR("/baz/wibble"));
	assert(wibble >= 0);
	printf("pre-opened %d as \"%s\"\n", wibble, TEST_DIR("/baz/wibble"));

	// CHECK: packed map into SHM [[SHMFD:[0-9]+]]
	shmfd = po_pack(map);
	printf("packed map into SHM %d\n", shmfd);

	// clear close-on-exec flag: we want this to be propagated!
	fcntl(shmfd, F_SETFD, 0);

	// CHECK: set LIB_PO_MAP=[[SHMFD]]
	snprintf(buffer, sizeof(buffer),"%d", shmfd);
	setenv("LIB_PO_MAP", buffer, 1);
	printf("set LIB_PO_MAP=%s\n", getenv("LIB_PO_MAP"));

	fflush(stdout);

	execve(argv[1], argv+1, environ);
	err(-1, "failed to execute '%s'", argv[1]);

	return 0;
}

#else

#include <sys/stat.h>

static bool print(const char *, int, cap_rights_t);

int main(int argc, char *argv[])
{
	char buffer[1024];
	struct stat st;
	struct po_map *map;
	int fd, i, shmfd;

	// CHECK: {{.*}}.child
	printf("----------------------------------------"
		"----------------------------------------\n");
	printf("%s\n", argv[0]);
	printf("----------------------------------------"
		"----------------------------------------\n");

	// CHECK: got shmfd: [[SHMFD]]
	shmfd = atoi(getenv("LIB_PO_MAP"));
	printf("got shmfd: %d\n", shmfd);

	// CHECK: unpacked map: [[MAP:0x[0-9a-f]+]]
	map = po_unpack(shmfd);
	printf("unpacked map: %p\n", map);
	po_map_set(map);

	// CHECK: contents of [[MAP]]:
	// CHECK-DAG: [[FOO]]: "foo"
	// CHECK-DAG: [[WIBBLE]]: "[[WIBBLE_PATH]]"
	printf("contents of %p:\n", map);
	po_map_foreach(map, print);

	// CHECK: hi.txt size: 3
	i = stat("foo/bar/hi.txt", &st);
	if (i != 0) {
		errx(-1, "stat(\"foo/bar/hi.txt\") failed");
	}
	printf("hi.txt size: %lu\n", st.st_size);

	// CHECK-NEXT: hi.txt contents: hi
	fd = open("foo/bar/hi.txt", O_RDONLY);
	read(fd, buffer, sizeof(buffer));
	printf("hi.txt contents: %s\n", buffer);

	// CHECK: bye.txt size: 4
	i = stat(TEST_DIR("/baz/wibble") "/bye.txt", &st);
	if (i != 0) {
		errx(-1, "stat(TEST_DIR(\"/baz/wibble\")) failed");
	}
	printf("bye.txt size: %lu\n", st.st_size);

	// CHECK-NEXT: bye.txt contents: bye
	fd = open(TEST_DIR("/baz/wibble") "/bye.txt", O_RDONLY);
	read(fd, buffer, sizeof(buffer));
	printf("bye.txt contents: %s\n", buffer);

	// CHECK: non-existent: -1
	fd = open("non-existent", O_RDONLY);
	printf("non-existent: %d\n", fd);

	return 0;
}

static bool
print(const char *dirname, int dirfd, cap_rights_t rights)
{
	printf("\t%d: \"%s\"\n", dirfd, dirname);
	return (true);
}

#endif
