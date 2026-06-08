#include "obj.h"
#include <fstream>
#include <sstream>

std::vector<model_tri> load_obj(const std::string &path) {
  std::vector<vec3f> pos;
  std::vector<model_tri> tris;
  std::ifstream in(path);
  if(!in) return tris;

  std::string line;
  while(std::getline(in, line)) {
    if(line.size() < 2) continue;

    if(line[0] == 'v' && line[1] == ' ') {
      std::istringstream ss(line.substr(2));
      vec3f v{};
      ss >> v.x >> v.y >> v.z;
      pos.push_back(v);
    }
    else if(line[0] == 'f' && line[1] == ' ') {
      std::istringstream ss(line.substr(2));
      std::string tok;
      std::vector<int> idx;
      while(ss >> tok) {
        // an 'f' token is v, v/vt, v//vn or v/vt/vn -- take the vertex index
        std::string istr = tok.substr(0, tok.find('/'));
        if(istr.empty()) continue;
        int i = std::stoi(istr);
        i = (i < 0) ? (int)pos.size() + i : i - 1;   // OBJ is 1-based; <0 is relative
        idx.push_back(i);
      }
      // fan-triangulate the polygon
      for(size_t k = 1; k + 1 < idx.size(); ++k) {
        int a = idx[0], b = idx[k], c = idx[k+1];
        if(a < 0 || b < 0 || c < 0) continue;
        if(a >= (int)pos.size() || b >= (int)pos.size() || c >= (int)pos.size()) continue;
        tris.push_back({pos[a], pos[b], pos[c]});
      }
    }
  }
  return tris;
}
