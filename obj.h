#ifndef OBJ_H
#define OBJ_H

#include <array>
#include <vector>
#include <string>

struct vec3f { float x, y, z; };
struct vec2f { float u, v; };

// A model-space vertex: position, texture coordinate (0,0 if the face has no
// vt), and normal (0,0,0 if the face has no vn).
struct model_vert { vec3f pos; vec2f uv; vec3f nrm; };

// A model-space triangle.
typedef std::array<model_vert, 3> model_tri;

// Load a Wavefront OBJ, triangulating polygon faces (fan).  'v', 'vt' and
// 'f' lines are used; vertex normals ('vn') are ignored.  Returns
// model-space triangles (empty if the file cannot be read).
std::vector<model_tri> load_obj(const std::string &path);

#endif
