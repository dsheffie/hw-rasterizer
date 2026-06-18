#include "obj.h"
#include <fstream>
#include <sstream>

std::vector<model_tri> load_obj(const std::string &path) {
  std::vector<vec3f> pos;
  std::vector<vec2f> tex;
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
    else if(line[0] == 'v' && line[1] == 't') {
      std::istringstream ss(line.substr(3));
      vec2f t{};
      ss >> t.u >> t.v;
      tex.push_back(t);
    }
    else if(line[0] == 'f' && line[1] == ' ') {
      std::istringstream ss(line.substr(2));
      std::string tok;
      std::vector<int> vi, ti;
      while(ss >> tok) {
        // an 'f' token is v, v/vt, v//vn or v/vt/vn
        size_t s0 = tok.find('/');
        std::string istr = tok.substr(0, s0);
        if(istr.empty()) continue;
        int i = std::stoi(istr);
        i = (i < 0) ? (int)pos.size() + i : i - 1;   // OBJ is 1-based; <0 is relative
        vi.push_back(i);

        // texture index: the token between the first and second '/'
        int t = -1;
        if(s0 != std::string::npos) {
          size_t s1 = tok.find('/', s0 + 1);
          std::string tstr = tok.substr(s0 + 1, s1 - (s0 + 1));
          if(!tstr.empty()) {
            t = std::stoi(tstr);
            t = (t < 0) ? (int)tex.size() + t : t - 1;
          }
        }
        ti.push_back(t);
      }
      // fan-triangulate the polygon
      for(size_t k = 1; k + 1 < vi.size(); ++k) {
        int a = vi[0], b = vi[k], c = vi[k+1];
        if(a < 0 || b < 0 || c < 0) continue;
        if(a >= (int)pos.size() || b >= (int)pos.size() || c >= (int)pos.size()) continue;
        auto uv = [&](int idx) -> vec2f {
          return (idx >= 0 && idx < (int)tex.size()) ? tex[idx] : vec2f{0.0f, 0.0f};
        };
        tris.push_back({ model_vert{pos[a], uv(ti[0])},
                         model_vert{pos[b], uv(ti[k])},
                         model_vert{pos[c], uv(ti[k+1])} });
      }
    }
  }
  return tris;
}
