#ifndef OBJ_H
#define OBJ_H

#include <array>
#include <vector>
#include <string>

struct vec3f { float x, y, z; };

// A model-space triangle (vertex positions only).
typedef std::array<vec3f, 3> model_tri;

// Load a Wavefront OBJ, triangulating polygon faces (fan).  Only 'v' and
// 'f' lines are used; texture/normal indices in 'f' tokens are ignored.
// Returns model-space triangles (empty if the file cannot be read).
std::vector<model_tri> load_obj(const std::string &path);

#endif
