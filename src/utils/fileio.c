//
//  fileio.c
//  C-ray
//
//  Created by Valtteri Koskivuori on 28/02/2015.
//  Copyright © 2015-2022 Valtteri Koskivuori. All rights reserved.
//

#include "../includes.h"
#include "fileio.h"
#include "../utils/logging.h"
#include "assert.h"
#ifndef WINDOWS
#include <libgen.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "string.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "filecache.h"
#include "textbuffer.h"

static char *getFileExtension(const char *fileName) {
	char buf[LINEBUFFER_MAXSIZE];
	lineBuffer line = { .buf = buf };
	fillLineBuffer(&line, fileName, '.');
	if (line.amountOf.tokens != 2) {
		return NULL;
	}
	char *extension = stringCopy(lastToken(&line));
	return extension;
}

enum fileType match_file_type(const char *ext) {
	if (!ext) return unknown;
	if (stringEquals(ext, "bmp"))
		return bmp;
	if (stringEquals(ext, "png"))
		return png;
	if (stringEquals(ext, "hdr"))
		return hdr;
	if (stringEquals(ext, "obj"))
		return obj;
	if (stringEquals(ext, "mtl"))
		return mtl;
	if (stringEquals(ext, "jpg"))
		return jpg;
	if (stringEquals(ext, "tiff"))
		return tiff;
	if (stringEquals(ext, "qoi"))
		return qoi;
	if (stringEquals(ext, "gltf"))
		return gltf;
	if (stringEquals(ext, "glb"))
		return glb;
	return unknown;
}

enum fileType guess_file_type(const char *filePath) {
	char *fileName = get_file_name(filePath);
	char *extension = getFileExtension(fileName);
	char *lower = stringToLower(extension);
	free(extension);
	extension = lower;
	free(fileName);
	enum fileType type = match_file_type(extension);
	free(extension);
	return type;
}

char *load_file(const char *filePath, size_t *bytes, struct file_cache *cache) {
	if (cache && cache_contains(cache, filePath)) return cache_load(cache, filePath, bytes);
	FILE *file = fopen(filePath, "rb");
	if (!file) {
		logr(warning, "Can't access '%.*s': %s\n", (int)strlen(filePath), filePath, strerror(errno));
		return NULL;
	}
	size_t fileBytes = get_file_size(filePath);
	if (!fileBytes) {
		fclose(file);
		return NULL;
	}
	char *buf = malloc(fileBytes + 1 * sizeof(char));
	size_t readBytes = fread(buf, sizeof(char), fileBytes, file);
	ASSERT(readBytes == fileBytes);
	if (ferror(file) != 0) {
		logr(warning, "Error reading file\n");
	} else {
		buf[fileBytes] = '\0';
	}
	fclose(file);
	if (bytes) *bytes = readBytes;
	if (cache) cache_store(cache, filePath, buf, readBytes);
	return buf;
}

void write_file(const unsigned char *buf, size_t bufsize, const char *filePath) {
	FILE *file = fopen(filePath, "wb" );
	char *backupPath = NULL;
	if(!file) {
		char *name = get_file_name(filePath);
		backupPath = stringConcat("./", name);
		free(name);
		file = fopen(backupPath, "wb");
		if (file) {
			char *path = get_file_path(filePath);
			logr(warning, "The specified output directory \"%s\" was not writeable, dumping the file in CWD instead.\n", path);
			free(path);
		} else {
			logr(warning, "Neither the specified output directory nor the current working directory were writeable. Image can't be saved. Fix your permissions!");
			return;
		}
	}
	logr(info, "Saving result in %s\'%s\'%s\n", KGRN, backupPath ? backupPath : filePath, KNRM);
	fwrite(buf, 1, bufsize, file);
	fclose(file);
	
	//We determine the file size after saving, because the lodePNG library doesn't have a way to tell the compressed file size
	//This will work for all image formats
	unsigned long bytes = get_file_size(backupPath ? backupPath : filePath);
	char *sizeString = human_file_size(bytes);
	logr(info, "Wrote %s to file.\n", sizeString);
	free(sizeString);
}


bool is_valid_file(char *path, struct file_cache *cache) {
#ifndef WINDOWS
	struct stat path_stat = { 0 };
	stat(path, &path_stat);
	return (S_ISREG(path_stat.st_mode) || (cache && cache_contains(cache, path)));
#else
	FILE *f = fopen(path, "r");
	if (f) {
		fclose(f);
		return true;
	}
	return false;
#endif
}

void wait_for_stdin(int seconds) {
#ifndef WINDOWS
	fd_set set;
	struct timeval timeout;
	int rv;
	FD_ZERO(&set);
	FD_SET(0, &set);
	timeout.tv_sec = seconds;
	timeout.tv_usec = 1000;
	rv = select(1, &set, NULL, NULL, &timeout);
	if (rv == -1) {
		logr(error, "Error on stdin timeout\n");
	} else if (rv == 0) {
		logr(error, "No input found after %i seconds. Hint: Try `./bin/c-ray input/scene.json`.\n", seconds);
	} else {
		return;
	}
#endif
}

/**
 Extract the filename from a given file path

 @param input File path to be processed
 @return Filename string, including file type extension
 */
//FIXME: Just return a pointer to the first byte of the filename? Why do we do all this
char *get_file_name(const char *input) {
	//FIXME: We're doing two copies here, maybe just rework the algorithm instead.
	char *copy = stringCopy(input);
	char *fn;
	
	/* handle trailing '/' e.g.
	 input == "/home/me/myprogram/" */
	if (copy[(strlen(copy) - 1)] == '/')
		copy[(strlen(copy) - 1)] = '\0';
	
	(fn = strrchr(copy, '/')) ? ++fn : (fn = copy);
	
	char *ret = stringCopy(fn);
	free(copy);
	
	return ret;
}

//For Windows
#define CRAY_PATH_MAX 4096

char *get_file_path(const char *input) {
	char *dir = NULL;
#ifdef WINDOWS
	dir = calloc(_MAX_DIR, sizeof(*dir));
	_splitpath_s(input, NULL, 0, dir, _MAX_DIR, NULL, 0, NULL, 0);
	return dir;
#else
	char *inputCopy = stringCopy(input);
	dir = stringCopy(dirname(inputCopy));
	free(inputCopy);
	char *final = stringConcat(dir, "/");
	free(dir);
	return final;
#endif
}

#define chunksize 65536
//Get scene data from stdin and return a pointer to it
char *read_stdin(size_t *bytes) {
	wait_for_stdin(2);
	
	char chunk[chunksize];
	
	size_t buf_size = 0;
	char *buf = NULL;
	int stdin_fd = fileno(stdin);
	int read_bytes = 0;
	while ((read_bytes = read(stdin_fd, &chunk, chunksize)) > 0) {
		char *old = buf;
		buf = realloc(buf, buf_size + read_bytes + 1);
		if (!buf) {
			logr(error, "Failed to realloc stdin buffer\n");
			free(old);
			return NULL;
		}
		memcpy(buf + buf_size, chunk, read_bytes);
		buf_size += read_bytes;
	}
	
	if (ferror(stdin)) {
		logr(error, "Failed to read from stdin\n");
		free(buf);
		return NULL;
	}
	
	if (bytes) *bytes = buf_size - 1;
	buf[buf_size ] = 0;
	return buf;
}

char *human_file_size(unsigned long bytes) {
	double kilobytes, megabytes, gigabytes, terabytes, petabytes, exabytes, zettabytes, yottabytes; // <- Futureproofing?!
	kilobytes  = bytes      / 1000.0;
	megabytes  = kilobytes  / 1000.0;
	gigabytes  = megabytes  / 1000.0;
	terabytes  = gigabytes  / 1000.0;
	petabytes  = terabytes  / 1000.0;
	exabytes   = petabytes  / 1000.0;
	zettabytes = exabytes   / 1000.0;
	yottabytes = zettabytes / 1000.0;
	
	// Okay, okay. In reality, this never gets even close to a zettabyte,
	// it'll overflow at around 18 exabytes.
	// I *did* get it to go to yottabytes using __uint128_t, but that's
	// not in C99. Maybe in the future.
	
	char *buf = calloc(64, sizeof(*buf));
	
	if (zettabytes >= 1000) {
		sprintf(buf, "%.02fYB", yottabytes);
	} else if (exabytes >= 1000) {
		sprintf(buf, "%.02fZB", zettabytes);
	} else if (petabytes >= 1000) {
		sprintf(buf, "%.02fEB", exabytes);
	} else if (terabytes >= 1000) {
		sprintf(buf, "%.02fPB", petabytes);
	} else if (gigabytes >= 1000) {
		sprintf(buf, "%.02fTB", terabytes);
	} else if (megabytes >= 1000) {
		sprintf(buf, "%.02fGB", gigabytes);
	} else if (kilobytes >= 1000) {
		sprintf(buf, "%.02fMB", megabytes);
	} else if (bytes >= 1000) {
		sprintf(buf, "%.02fkB", kilobytes);
	} else {
		sprintf(buf, "%ldB", bytes);
	}
	return buf;
}

size_t get_file_size(const char *fileName) {
	FILE *file = fopen(fileName, "r");
	if (!file) return 0;
	fseek(file, 0L, SEEK_END);
	size_t size = ftell(file);
	fclose(file);
	return size;
}
