/*-
 * Copyright (c) 2016 Stanley Uche Godfrey
 * Copyright (c) 2018 Jonathan Anderson
 * All rights reserved.
 *
 * This software was developed at Memorial University under the
 * NSERC Discovery program (RGPIN-2015-06048).
 *
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

/**
 * @file   po_libc_wrappers.c
 * @brief  Wrappers of libc functions that access global variables.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "internal.h"

/**
 * A default po_map that can be used implicitly by libc wrappers.
 *
 * @internal
 */
static struct po_map *global_map;

/**
 * Find a relative path within the po_map given by SHARED_MEMORYFD (if it
 * exists).
 *
 * @returns  a struct po_relpath with dirfd and relative_path as set by po_find
 *           if there is an available po_map, or AT_FDCWD/path otherwise
 */
static struct po_relpath find_relative(const char *path, cap_rights_t *);

/**
 * Get the map that was handed into the process via `SHARED_MEMORYFD`
 * (if it exists).
 */
static struct po_map*	get_shared_map(void);

/**
 * Capability-safe wrapper around the `access(2)` system call.
 *
 * `access(2)` accepts a path argument that can reference the global filesystem
 * namespace. This is not a capability-safe operation, so this wrapper function
 * attempts to look up the path (or a prefix of it) within the current global
 * po_map and converts the call into the capability-safe `faccessat(2)` if
 * possible. If the current po_map does not contain the sought-after path,
 * this wrapper will call `faccessat(AT_FDCWD, original_path, ...)`, which is
 * the same as the unwrapped `access(2)` call.
 */
int
access(const char *path, int mode)
{
	struct po_relpath rel = find_relative(path, NULL);

	return faccessat(rel.dirfd, rel.relative_path, mode,0);
}

/**
 * Capability-safe wrapper around the `_open(2)` system call.
 *
 * `_open(2)` accepts a path argument that can reference the global filesystem
 * namespace. This is not a capability-safe operation, so this wrapper function
 * attempts to look up the path (or a prefix of it) within the current global
 * po_map and converts the call into the capability-safe `openat(2)` if
 * possible. If the current po_map does not contain the sought-after path,
 * this wrapper will call `openat(AT_FDCWD, original_path, ...)`, which is
 * the same as the unwrapped `open(2)` call.
 */
int
_open(const char *path, int flags, ...)
{
	struct po_relpath rel;
	va_list args;
	int mode;

	va_start(args, flags);
	mode = va_arg(args, int);
	rel = find_relative(path, NULL);

	// If the file is already opened, no need of relative opening!
	if( strcmp(rel.relative_path,".") == 0 )
		return dup(rel.dirfd);
	else
		return openat(rel.dirfd, rel.relative_path, flags, mode);	
}

/**
 * Capability-safe wrapper around the `open(2)` system call.
 *
 * `open(2)` will behave just like `_open(2)` if the varargs are unpacked and
 *  passed.
 */
int
open(const char *path, int flags, ...)
{
	va_list args;
	int mode;

	va_start(args, flags);
	mode = va_arg(args, int);
    return _open(path, flags, mode);
}

/**
 * Capability-safe wrapper around the `stat(2)` system call.
 *
 * `stat(2)` accepts a path argument that can reference the global filesystem
 * namespace. This is not a capability-safe operation, so this wrapper function
 * attempts to look up the path (or a prefix of it) within the current global
 * po_map and converts the call into the capability-safe `fstatat(2)` if
 * possible. If the current po_map does not contain the sought-after path,
 * this wrapper will call `fstatat(AT_FDCWD, original_path, ...)`, which is
 * the same as the unwrapped `stat(2)` call.
 */
int
stat(const char *path, struct stat *st)
{
	struct po_relpath rel = find_relative(path, NULL);

	return fstatat(rel.dirfd, rel.relative_path,st,AT_SYMLINK_NOFOLLOW);
}

int
getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo *(*res))
{
	struct po_map *map;
	map = get_shared_map;
	if(map == NULL)
	{
		return NULL;
	} 
	else 
	{
		
		for(size_t i = 0; i < map->length; i++) 
		{

		const struct po_map_entry *entry = map->entries + i;
		if((entry->name) != NULL) const char *name = entry->name;

		if( (strcmp(name, node) == 0) && entry->flag == PREOP_SOCKET)
			{
				(*res)->ai_flags = 1000;
				(*res)->ai_family = 1000;
				(*res)->ai_socktype = 1000;
				(*res)->ai_protocol = 1000;
				(*res)->ai_addrlen = 1000;
				(*res)->ai_addr->sa_family = entry->fd;
				strcpy((*res)->ai_addr->sa_data, "passed");
				return 0;
			}
		}

		return EAI_SYSTEM;
	}
}

int 
connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	if ( strcmp((addr->sa_data), "passed") == 0 )
	{
		dup2(sockfd, (int)(addr->sa_family));
		return 0;
	}
	return -1;
}

void
po_set_libc_map(struct po_map *map)
{
	po_map_assertvalid(map);

	map->refcount += 1;

	if (global_map != NULL) {
		po_map_release(global_map);
	}

	global_map = map;
}

static struct po_relpath
find_relative(const char *path, cap_rights_t *rights)
{
	struct po_relpath rel;
	struct po_map *map;

	map = get_shared_map();
	if (map == NULL) {
		rel.dirfd = AT_FDCWD;
		rel.relative_path = path;
	} else {
		rel = po_find(map, path, NULL);
	}

	return (rel);
}

static struct po_map*
get_shared_map()
{
	struct po_map *map;
	char *end, *env;
	long fd;

	// Do we already have a default map?
	if (global_map) {
		po_map_assertvalid(global_map);
		return (global_map);
	}

	// Attempt to unwrap po_map from a shared memory segment specified by
	// SHARED_MEMORYFD
	env = getenv("SHARED_MEMORYFD");
	if (env == NULL || *env == '\0') {
		return (NULL);
	}

	// We expect this environment variable to be an integer and nothing but
	// an integer.
	fd = strtol(env, &end, 10);
	if (*end != '\0') {
		return (NULL);
	}

	map = po_unpack(fd);
	if (map == NULL) {
		return (NULL);
	}

	global_map = map;

	return (map);
}
