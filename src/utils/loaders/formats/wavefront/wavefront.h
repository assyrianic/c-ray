//
//  wavefront.h
//  c-ray
//
//  Created by Valtteri Koskivuori on 02/04/2019.
//  Copyright © 2019-2023 Valtteri Koskivuori. All rights reserved.
//

#pragma once

struct file_cache;

struct mesh_arr parse_wavefront(const char *file_path, struct file_cache *cache);
