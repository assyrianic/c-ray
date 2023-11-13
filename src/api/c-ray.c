//
//  c-ray.c
//  c-ray
//
//  Created by Valtteri on 5.1.2020.
//  Copyright © 2020-2023 Valtteri Koskivuori. All rights reserved.
//

#include "../includes.h"
#include <c-ray/c-ray.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>

#include "../renderer/renderer.h"
#include "../datatypes/scene.h"
#include "../utils/gitsha1.h"
#include "../utils/logging.h"
#include "../utils/fileio.h"
#include "../utils/platform/terminal.h"
#include "../utils/assert.h"
#include "../datatypes/image/texture.h"
#include "../utils/args.h"
#include "../utils/string.h"
#include "../utils/protocol/server.h"
#include "../utils/protocol/worker.h"
#include "../utils/filecache.h"
#include "../utils/hashtable.h"
#include "../datatypes/camera.h"
#include "../utils/loaders/textureloader.h"

#ifdef CRAY_DEBUG_ENABLED
#define DEBUG "D"
#else
#define DEBUG ""
#endif

#define VERSION "0.6.3"DEBUG

char *cr_get_version() {
	return VERSION;
}

char *cr_get_git_hash() {
	return gitHash();
}

char *cr_get_file_path(char *full_path) {
	return get_file_path(full_path);
}

// -- Renderer --

struct cr_renderer;

struct cr_renderer *cr_new_renderer() {
	return (struct cr_renderer *)renderer_new();
}

bool cr_renderer_set_num_pref(struct cr_renderer *ext, enum cr_renderer_param p, uint64_t num) {
	if (!ext) return false;
	struct renderer *r = (struct renderer *)ext;
	switch (p) {
		case cr_renderer_threads: {
			r->prefs.threads = num;
			return true;
		}
		case cr_renderer_samples: {
			r->prefs.sampleCount = num;
			return true;
		}
		case cr_renderer_bounces: {
			if (num > 512) return false;
			r->prefs.bounces = num;
			return true;
		}
		case cr_renderer_tile_width: {
			r->prefs.tileWidth = num;
			return true;
		}
		case cr_renderer_tile_height: {
			r->prefs.tileHeight = num;
			return true;
		}
		case cr_renderer_output_num: {
			r->prefs.imgCount = num;
			return true;
		}
		case cr_renderer_override_width: {
			r->prefs.override_width = num;
			return true;
		}
		case cr_renderer_override_height: {
			r->prefs.override_height = num;
			return true;
		}
		case cr_renderer_override_cam: {
			r->prefs.selected_camera = num;
			return true;
		}
		case cr_renderer_is_iterative: {
			r->prefs.iterative = true;
			return true;
		}
		default: {
			logr(warning, "Renderer param %i not a number\n", p);
		}
	}
	return false;
}

bool cr_renderer_set_str_pref(struct cr_renderer *ext, enum cr_renderer_param p, const char *str) {
	if (!ext) return false;
	struct renderer *r = (struct renderer *)ext;
	switch (p) {
		case cr_renderer_tile_order: {
			if (stringEquals(str, "random")) {
				r->prefs.tileOrder = ro_random;
			} else if (stringEquals(str, "topToBottom")) {
				r->prefs.tileOrder = ro_top_to_bottom;
			} else if (stringEquals(str, "fromMiddle")) {
				r->prefs.tileOrder = ro_from_middle;
			} else if (stringEquals(str, "toMiddle")) {
				r->prefs.tileOrder = ro_to_middle;
			} else {
				r->prefs.tileOrder = ro_normal;
			}
			return true;
		}
		case cr_renderer_output_path: {
			if (r->prefs.imgFilePath) free(r->prefs.imgFilePath);
			r->prefs.imgFilePath = stringCopy(str);
			return true;
		}
		case cr_renderer_asset_path: {
			if (r->prefs.assetPath) free(r->prefs.assetPath);
			r->prefs.assetPath = stringCopy(str);
			return true;
		}
		case cr_renderer_output_name: {
			if (r->prefs.imgFileName) free(r->prefs.imgFileName);
			r->prefs.imgFileName = stringCopy(str);
			return true;
		}
		case cr_renderer_output_filetype: {
			if (stringEquals(str, "bmp")) {
				r->prefs.imgType = bmp;
			} else if (stringEquals(str, "png")) {
				r->prefs.imgType = png;
			} else if (stringEquals(str, "qoi")) {
				r->prefs.imgType = qoi;
			} else {
				return false;
			}
			return true;
		}
		case cr_renderer_node_list: {
			if (r->prefs.node_list) free(r->prefs.node_list);
			r->prefs.node_list = stringCopy(str);
			if (!r->state.file_cache) r->state.file_cache = calloc(1, sizeof(*r->state.file_cache));
			return true;
		}
		case cr_renderer_scene_cache: {
			if (r->sceneCache) free(r->sceneCache);
			r->sceneCache = stringCopy(str);
			return true;
		}
		default: {
			logr(warning, "Renderer param %i not a string\n", p);
		}
	}
	return false;
}

const char *cr_renderer_get_str_pref(struct cr_renderer *ext, enum cr_renderer_param p) {
	if (!ext) return NULL;
	struct renderer *r = (struct renderer *)ext;
	switch (p) {
		case cr_renderer_output_path: return r->prefs.imgFilePath;
		case cr_renderer_output_name: return r->prefs.imgFileName;
		default: return NULL;
	}
	return NULL;
}

uint64_t cr_renderer_get_num_pref(struct cr_renderer *ext, enum cr_renderer_param p) {
	if (!ext) return 0;
	struct renderer *r = (struct renderer *)ext;
	switch (p) {
		case cr_renderer_threads: return r->prefs.threads;
		case cr_renderer_samples: return r->prefs.sampleCount;
		case cr_renderer_bounces: return r->prefs.bounces;
		case cr_renderer_tile_width: return r->prefs.tileWidth;
		case cr_renderer_tile_height: return r->prefs.tileHeight;
		case cr_renderer_output_num: return r->prefs.imgCount;
		case cr_renderer_override_width: return r->prefs.override_width;
		case cr_renderer_override_height: return r->prefs.override_height;
		case cr_renderer_should_save: return r->state.saveImage ? 1 : 0;
		case cr_renderer_output_filetype: return r->prefs.imgType;
		default: return 0; // TODO
	}
	return 0;
}

bool cr_scene_set_background_hdr(struct cr_renderer *r_ext, struct cr_scene *s_ext, const char *hdr_filename) {
	if (!r_ext || !s_ext) return false;
	struct renderer *r = (struct renderer *)r_ext;
	struct world *w = (struct world *)s_ext;
	char *full_path = stringConcat(r->prefs.assetPath, hdr_filename);
	if (is_valid_file(full_path, r->state.file_cache)) {
		w->background = newBackground(&w->storage, newImageTexture(&w->storage, load_texture(full_path, &w->storage.node_pool, r->state.file_cache), 0), NULL);
		free(full_path);
		return true;
	}
	free(full_path);
	return false;
}

bool cr_scene_set_background(struct cr_scene *s_ext, struct cr_color *down, struct cr_color *up) {
	if (!s_ext) return false;
	struct world *s = (struct world *)s_ext;
	if (down && up) {
		s->background = newBackground(&s->storage, newGradientTexture(&s->storage, *(struct color *)down, *(struct color *)up), NULL);
		return true;
	} else {
		s->background = newBackground(&s->storage, NULL, NULL);
	}
	return false;
}

void cr_destroy_renderer(struct cr_renderer *ext) {
	struct renderer *r = (struct renderer *)ext;
	ASSERT(r);
	renderer_destroy(r);
}

// -- Scene --

struct cr_scene;

// Do we want multiple scenes anyway?
struct cr_scene *cr_scene_create(struct cr_renderer *r) {
	(void)r;
	return NULL;
}

void cr_scene_destroy(struct cr_scene *s) {
	//TODO
	(void)s;
}

struct cr_object;

struct cr_object *cr_object_new(struct cr_scene *s) {
	(void)s;
	return NULL;
}

struct cr_instance;

struct cr_instance *cr_instance_new(struct cr_object *o) {
	(void)o;
	return NULL;
}

// -- Camera --

struct camera default_camera = {
	.FOV = 80.0f,
	.focus_distance = 0.0f,
	.fstops = 0.0f,
	.width = 800,
	.height = 600,
};

cr_camera cr_camera_new(struct cr_scene *ext) {
	if (!ext) return -1;
	struct world *scene = (struct world *)ext;
	return camera_arr_add(&scene->cameras, default_camera);
}

bool cr_camera_set_num_pref(struct cr_scene *ext, cr_camera c, enum cr_camera_param p, double num) {
	if (c < 0 || !ext) return false;
	struct world *scene = (struct world *)ext;
	if ((size_t)c > scene->cameras.count - 1) return false;
	struct camera *cam = &scene->cameras.items[c];
	switch (p) {
		case cr_camera_fov: {
			cam->FOV = num;
			return true;
		}
		case cr_camera_focus_distance: {
			cam->focus_distance = num;
			return true;
		}
		case cr_camera_fstops: {
			cam->fstops = num;
			return true;
		}
		case cr_camera_pose_x: {
			cam->position.x = num;
			return true;
		}
		case cr_camera_pose_y: {
			cam->position.y = num;
			return true;
		}
		case cr_camera_pose_z: {
			cam->position.z = num;
			return true;
		}
		case cr_camera_pose_roll: {
			cam->orientation.roll = num;
			return true;
		}
		case cr_camera_pose_pitch: {
			cam->orientation.pitch = num;
			return true;
		}
		case cr_camera_pose_yaw: {
			cam->orientation.yaw = num;
			return true;
		}
		case cr_camera_time: {
			cam->time = num;
			return true;
		}
		case cr_camera_res_x: {
			cam->width = num;
			return true;
		}
		case cr_camera_res_y: {
			cam->height = num;
			return true;
		}
	}

	cam_update_pose(cam, &cam->orientation, &cam->position);
	return false;
}

bool cr_camera_update(struct cr_scene *ext, cr_camera c) {
	if (c < 0 || !ext) return false;
	struct world *scene = (struct world *)ext;
	if ((size_t)c > scene->cameras.count - 1) return false;
	struct camera *cam = &scene->cameras.items[c];
	cam_update_pose(cam, &cam->orientation, &cam->position);
	cam_recompute_optics(cam);
	return true;
}

bool cr_camera_remove(struct cr_scene *s, cr_camera c) {
	//TODO
	(void)s;
	(void)c;
	return false;
}

// --

void cr_load_mesh_from_file(char *file_path) {
	(void)file_path;
	ASSERT_NOT_REACHED();
}

void cr_load_mesh_from_buf(char *buf) {
	(void)buf;
	ASSERT_NOT_REACHED();
}

struct texture *cr_renderer_render(struct cr_renderer *ext) {
	struct renderer *r = (struct renderer *)ext;
	if (r->prefs.node_list) {
		r->state.clients = syncWithClients(r, &r->state.clientCount);
		free(r->sceneCache);
		r->sceneCache = NULL;
		cache_destroy(r->state.file_cache);
	}
	if (!r->state.clients && r->prefs.threads == 0) {
		logr(warning, "You specified 0 local threads, and no network clients were found. Nothing to do.\n");
		return NULL;
	}
	struct texture *image = renderFrame(r);
	return image;
}

void cr_start_render_worker(int port) {
	worker_start(port);
}

