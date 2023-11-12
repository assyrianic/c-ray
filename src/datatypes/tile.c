//
//  tile.c
//  C-ray
//
//  Created by Valtteri Koskivuori on 06/07/2018.
//  Copyright © 2018-2022 Valtteri Koskivuori. All rights reserved.
//

#include "../includes.h"
#include "../datatypes/image/imagefile.h"
#include "../renderer/renderer.h"
#include "tile.h"

#include "../utils/logging.h"
#include "../utils/platform/mutex.h"
#include "../vendored/pcg_basic.h"
#include <string.h>

static void reorderTiles(struct render_tile **tiles, unsigned tileCount, enum render_order tileOrder);

struct render_tile *tile_next(struct renderer *r) {
	struct render_tile *tile = NULL;
	mutex_lock(r->state.tileMutex);
	if (r->state.finishedTileCount < r->state.tileCount) {
		tile = &r->state.renderTiles[r->state.finishedTileCount];
		tile->state = rendering;
		tile->index = r->state.finishedTileCount++;
	} else {
		// If a network worker disappeared during render, finish those tiles locally here at the end
		for (size_t t = 0; t < r->state.tileCount; ++t) {
			if (r->state.renderTiles[t].state == rendering && r->state.renderTiles[t].network_renderer) {
				r->state.renderTiles[t].network_renderer = false;
				tile = &r->state.renderTiles[t];
				tile->state = rendering;
				tile->index = t;
				break;
			}
		}
	}
	mutex_release(r->state.tileMutex);
	return tile;
}

struct render_tile *tile_next_interactive(struct renderer *r) {
	struct render_tile *tile = NULL;
	mutex_lock(r->state.tileMutex);
	again:
	if (r->state.finishedPasses < r->prefs.sampleCount + 1) {
		if (r->state.finishedTileCount < r->state.tileCount) {
			tile = &r->state.renderTiles[r->state.finishedTileCount];
			tile->state = rendering;
			tile->index = r->state.finishedTileCount++;
		} else {
			r->state.finishedPasses++;
			r->state.finishedTileCount = 0;
			goto again;
		}
	}
	mutex_release(r->state.tileMutex);
	return tile;
}

unsigned tile_quantize(struct render_tile **renderTiles, unsigned width, unsigned height, unsigned tileWidth, unsigned tileHeight, enum render_order tileOrder) {
	
	logr(info, "Quantizing render plane\n");
	
	//Sanity check on tilesizes
	if (tileWidth >= width) tileWidth = width;
	if (tileHeight >= height) tileHeight = height;
	if (tileWidth <= 0) tileWidth = 1;
	if (tileHeight <= 0) tileHeight = 1;
	
	unsigned tilesX = width / tileWidth;
	unsigned tilesY = height / tileHeight;
	
	tilesX = (width % tileWidth) != 0 ? tilesX + 1: tilesX;
	tilesY = (height % tileHeight) != 0 ? tilesY + 1: tilesY;
	
	*renderTiles = calloc(tilesX*tilesY, sizeof(**renderTiles));
	if (*renderTiles == NULL) {
		logr(error, "Failed to allocate renderTiles array.\n");
		return 0;
	}
	
	int tileCount = 0;
	for (unsigned y = 0; y < tilesY; ++y) {
		for (unsigned x = 0; x < tilesX; ++x) {
			struct render_tile *tile = &(*renderTiles)[x + y * tilesX];
			tile->width  = tileWidth;
			tile->height = tileHeight;
			
			tile->begin.x = x       * tileWidth;
			tile->end.x   = (x + 1) * tileWidth;
			
			tile->begin.y = y       * tileHeight;
			tile->end.y   = (y + 1) * tileHeight;
			
			tile->end.x = min((x + 1) * tileWidth, width);
			tile->end.y = min((y + 1) * tileHeight, height);
			
			tile->width = tile->end.x - tile->begin.x;
			tile->height = tile->end.y - tile->begin.y;

			tile->state = ready_to_render;
			//Samples have to start at 1, so the running average works
			tile->index = tileCount++;
		}
	}
	logr(info, "Quantized image into %i tiles. (%ix%i)\n", (tilesX*tilesY), tilesX, tilesY);
	
	reorderTiles(renderTiles, tileCount, tileOrder);
	
	return tileCount;
}

static void reorderTopToBottom(struct render_tile **tiles, unsigned tileCount) {
	unsigned endIndex = tileCount - 1;
	
	struct render_tile *tempArray = calloc(tileCount, sizeof(*tempArray));
	
	for (unsigned i = 0; i < tileCount; ++i) {
		tempArray[i] = (*tiles)[endIndex--];
	}
	
	free(*tiles);
	*tiles = tempArray;
}

static unsigned int rand_interval(unsigned int min, unsigned int max, pcg32_random_t *rng) {
	unsigned int r;
	const unsigned int range = 1 + max - min;
	const unsigned int buckets = UINT32_MAX / range;
	const unsigned int limit = buckets * range;
	
	/* Create equal size buckets all in a row, then fire randomly towards
	 * the buckets until you land in one of them. All buckets are equally
	 * likely. If you land off the end of the line of buckets, try again. */
	do {
		r = pcg32_random_r(rng);
	} while (r >= limit);
	
	return min + (r / buckets);
}

static void reorderRandom(struct render_tile **tiles, unsigned tileCount) {
	pcg32_random_t rng;
	pcg32_srandom_r(&rng, 3141592, 0);
	for (unsigned i = 0; i < tileCount; ++i) {
		unsigned random = rand_interval(0, tileCount - 1, &rng);
		
		struct render_tile temp = (*tiles)[i];
		(*tiles)[i] = (*tiles)[random];
		(*tiles)[random] = temp;
	}
}

static void reorderFromMiddle(struct render_tile **tiles, unsigned tileCount) {
	int midLeft = 0;
	int midRight = 0;
	bool isRight = true;
	
	midRight = ceil(tileCount / 2);
	midLeft = midRight - 1;
	
	struct render_tile *tempArray = calloc(tileCount, sizeof(*tempArray));
	
	for (unsigned i = 0; i < tileCount; ++i) {
		if (isRight) {
			tempArray[i] = (*tiles)[midRight++];
			isRight = false;
		} else {
			tempArray[i] = (*tiles)[midLeft--];
			isRight = true;
		}
	}
	
	free(*tiles);
	*tiles = tempArray;
}

static void reorderToMiddle(struct render_tile **tiles, unsigned tileCount) {
	unsigned left = 0;
	unsigned right = 0;
	bool isRight = true;
	
	right = tileCount - 1;
	
	struct render_tile *tempArray = calloc(tileCount, sizeof(*tempArray));
	
	for (unsigned i = 0; i < tileCount; ++i) {
		if (isRight) {
			tempArray[i] = (*tiles)[right--];
			isRight = false;
		} else {
			tempArray[i] = (*tiles)[left++];
			isRight = true;
		}
	}
	
	free(*tiles);
	*tiles = tempArray;
}

static void reorderTiles(struct render_tile **tiles, unsigned tileCount, enum render_order tileOrder) {
	switch (tileOrder) {
		case ro_from_middle:
			reorderFromMiddle(tiles, tileCount);
			break;
		case ro_to_middle:
			reorderToMiddle(tiles, tileCount);
			break;
		case ro_top_to_bottom:
			reorderTopToBottom(tiles, tileCount);
			break;
		case ro_random:
			reorderRandom(tiles, tileCount);
			break;
		default:
			break;
	}
}
