#ifndef OBJ_H
#define OBJ_H

#include <array>
#include <vector>
#include <string>

struct vec3f { float x, y, z; };
struct vec2f { float u, v; };

// A model-space vertex: position plus texture coordinate (0,0 if the OBJ
// face carries no vt index).
struct model_vert { vec3f pos; vec2f uv; };

// A model-space triangle.
typedef std::array<model_vert, 3> model_tri;

// Load a Wavefront OBJ, triangulating polygon faces (fan).  'v', 'vt' and
// 'f' lines are used; vertex normals ('vn') are ignored.  Returns
// model-space triangles (empty if the file cannot be read).
std::vector<model_tri> load_obj(const std::string &path);

#endif
