//
//  wavefront.c
//  C-ray
//
//  Created by Valtteri Koskivuori on 02/04/2019.
//  Copyright © 2019-2022 Valtteri Koskivuori. All rights reserved.
//

#include <string.h>
#include "../../../../includes.h"
#include "../../../../datatypes/mesh.h"
#include "../../../../datatypes/vector.h"
#include "../../../../datatypes/poly.h"
#include "../../../../datatypes/material.h"
#include "../../../logging.h"
#include "../../../string.h"
#include "../../../fileio.h"
#include "../../../textbuffer.h"
#include "mtlloader.h"

#include "wavefront.h"

static int findMaterialIndex(struct material *materialSet, int materialCount, char *mtlName) {
	for (int i = 0; i < materialCount; ++i) {
		if (stringEquals(materialSet[i].name, mtlName)) {
			return i;
		}
	}
	return 0;
}

static struct vector parseVertex(lineBuffer *line) {
	ASSERT(line->amountOf.tokens == 4);
	return (struct vector){ atof(nextToken(line)), atof(nextToken(line)), atof(nextToken(line)) };
}

static struct coord parseCoord(lineBuffer *line) {
	// Some weird OBJ files just have a 0.0 as the third value for 2d coordinates.
	ASSERT(line->amountOf.tokens == 3 || line->amountOf.tokens == 4);
	return (struct coord){ atof(nextToken(line)), atof(nextToken(line)) };
}

// Wavefront supports different indexing types like
// f v1 v2 v3 [v4]
// f v1/vt1 v2/vt2 v3/vt3
// f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
// f v1//vn1 v2//vn2 v3//vn3
// Or a quad:
// f v1//vn1 v2//vn2 v3//vn3 v4//vn4
size_t parsePolygons(lineBuffer *line, struct poly *buf) {
	char container[LINEBUFFER_MAXSIZE];
	lineBuffer batch = { .buf = container };
	size_t polycount = line->amountOf.tokens - 3;
	// For now, c-ray will just translate quads to two polygons while parsing
	// Explode in a ball of fire if we encounter an ngon
	bool is_ngon = polycount > 2;
	if (is_ngon) {
		logr(debug, "!! Found an ngon in wavefront file, skipping !!\n");
		polycount = 2;
	}
	bool skipped = false;
	for (size_t i = 0; i < polycount; ++i) {
		firstToken(line);
		struct poly *p = &buf[i];
		p->vertexCount = MAX_CRAY_VERTEX_COUNT;
		for (int j = 0; j < p->vertexCount; ++j) {
			fillLineBuffer(&batch, nextToken(line), '/');
			if (batch.amountOf.tokens >= 1) p->vertexIndex[j] = atoi(firstToken(&batch));
			if (batch.amountOf.tokens >= 2) p->textureIndex[j] = atoi(nextToken(&batch));
			if (batch.amountOf.tokens >= 3) p->normalIndex[j] = atoi(nextToken(&batch));
			if (i == 1 && !skipped) {
				nextToken(line);
				skipped = true;
			}
		}
	}
	return polycount;
}

static int fixIndex(size_t max, int oldIndex) {
	if (oldIndex == 0) // Unused
		return -1;
	
	if (oldIndex < 0) // Relative to end of list
		return (int)max + oldIndex;
	
	return oldIndex - 1;// Normal indexing
}

static void fixIndices(struct poly *p, size_t totalVertices, size_t totalTexCoords, size_t totalNormals) {
	for (int i = 0; i < MAX_CRAY_VERTEX_COUNT; ++i) {
		p->vertexIndex[i] = fixIndex(totalVertices, p->vertexIndex[i]);
		p->textureIndex[i] = fixIndex(totalTexCoords, p->textureIndex[i]);
		p->normalIndex[i] = fixIndex(totalNormals, p->normalIndex[i]);
	}
}

float get_poly_area(struct poly *p, struct vector *vertices) {
	const struct vector v0 = vertices[p->vertexIndex[0]];
	const struct vector v1 = vertices[p->vertexIndex[1]];
	const struct vector v2 = vertices[p->vertexIndex[2]];

	const struct vector a = vec_sub(v1, v0);
	const struct vector b = vec_sub(v2, v0);

	const struct vector cross = vec_cross(a, b);
	return vec_length(cross) / 2.0f;
}

struct mesh *parseWavefront(const char *filePath, size_t *finalMeshCount, struct file_cache *cache) {
	size_t bytes = 0;
	char *rawText = load_file(filePath, &bytes, cache);
	if (!rawText) return NULL;
	logr(debug, "Loading OBJ at %s\n", filePath);
	textBuffer *file = newTextBuffer(rawText);
	char *assetPath = get_file_path(filePath);
	
	//Start processing line-by-line, state machine style.
	size_t meshCount = 1;
	//meshCount += count(file, "o");
	//meshCount += count(file, "g");
	//size_t currentMesh = 0;
	size_t valid_meshes = 0;
	
	struct material *materialSet = NULL;
	int materialCount = 0;
	int currentMaterialIndex = 0;
	
	//FIXME: Handle more than one mesh
	struct mesh *meshes = calloc(1, sizeof(*meshes));
	struct mesh *currentMeshPtr = NULL;
	
	currentMeshPtr = meshes;
	valid_meshes = 1;
	
	struct poly polybuf[2];
	float surface_area = 0.0f;

	char *head = firstLine(file);
	char buf[LINEBUFFER_MAXSIZE];
	lineBuffer line = { .buf = buf };
	while (head) {
		fillLineBuffer(&line, head, ' ');
		char *first = firstToken(&line);
		if (first[0] == '#') {
			head = nextLine(file);
			continue;
		} else if (first[0] == '\0') {
			head = nextLine(file);
			continue;
		} else if (first[0] == 'o' || first[0] == 'g') {
			//FIXME: o and g probably have a distinction for a reason?
			//currentMeshPtr = &meshes[currentMesh++];
			currentMeshPtr->name = stringCopy(peekNextToken(&line));
			//valid_meshes++;
		} else if (stringEquals(first, "v")) {
			vector_arr_add(&currentMeshPtr->vertices, parseVertex(&line));
		} else if (stringEquals(first, "vt")) {
			coord_arr_add(&currentMeshPtr->texture_coords, parseCoord(&line));
		} else if (stringEquals(first, "vn")) {
			vector_arr_add(&currentMeshPtr->normals, parseVertex(&line));
		} else if (stringEquals(first, "s")) {
			// Smoothing groups. We don't care about these, we always smooth.
		} else if (stringEquals(first, "f")) {
			size_t count = parsePolygons(&line, polybuf);
			for (size_t i = 0; i < count; ++i) {
				struct poly p = polybuf[i];
				//TODO: Check if we actually need the file totals here
				fixIndices(&p, currentMeshPtr->vertices.count, currentMeshPtr->texture_coords.count, currentMeshPtr->normals.count);
				surface_area += get_poly_area(&p, currentMeshPtr->vertices.items);
				p.materialIndex = currentMaterialIndex;
				p.hasNormals = p.normalIndex[0] != -1;
				poly_arr_add(&currentMeshPtr->polygons, p);
			}
		} else if (stringEquals(first, "usemtl")) {
			currentMaterialIndex = findMaterialIndex(materialSet, materialCount, peekNextToken(&line));
		} else if (stringEquals(first, "mtllib")) {
			char *mtlFilePath = stringConcat(assetPath, peekNextToken(&line));
			windowsFixPath(mtlFilePath);
			materialSet = parseMTLFile(mtlFilePath, &materialCount, cache);
			free(mtlFilePath);
		} else {
			char *fileName = get_file_name(filePath);
			logr(debug, "Unknown statement \"%s\" in OBJ \"%s\" on line %zu\n",
				 first, fileName, file->current.line);
			free(fileName);
		}
		head = nextLine(file);
	}
	
	if (finalMeshCount) *finalMeshCount = valid_meshes;
	destroyTextBuffer(file);
	free(rawText);
	free(assetPath);

	if (materialSet) {
		for (size_t i = 0; i < meshCount; ++i) {
			for (size_t m = 0; m < materialCount; ++m) {
				material_arr_add(&meshes[i].materials, materialSet[m]);
			}
		}
	} else {
		for (size_t i = 0; i < meshCount; ++i) {
			material_arr_add(&meshes[i].materials, warningMaterial());
		}
	}

	logr(debug, "Mesh %s surface area is %.4fm²\n", currentMeshPtr->name, (double)surface_area);
	
	currentMeshPtr->surface_area = surface_area;
	return meshes;
}
