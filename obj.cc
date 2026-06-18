#include "obj.h"
#include <fstream>
#include <sstream>

std::vector<model_tri> load_obj(const std::string &path) {
  std::vector<vec3f> pos;
  std::vector<vec2f> tex;
  std::vector<vec3f> nrm;
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
    else if(line[0] == 'v' && line[1] == 'n') {
      std::istringstream ss(line.substr(3));
      vec3f n{};
      ss >> n.x >> n.y >> n.z;
      nrm.push_back(n);
    }
    else if(line[0] == 'f' && line[1] == ' ') {
      std::istringstream ss(line.substr(2));
      std::string tok;
      std::vector<int> vi, ti, ni;
      while(ss >> tok) {
        // an 'f' token is v, v/vt, v//vn or v/vt/vn
        size_t s0 = tok.find('/');
        std::string istr = tok.substr(0, s0);
        if(istr.empty()) continue;
        int i = std::stoi(istr);
        i = (i < 0) ? (int)pos.size() + i : i - 1;   // OBJ is 1-based; <0 is relative
        vi.push_back(i);

        // texture index (between the two '/'), then normal index (after the 2nd)
        int t = -1, n = -1;
        size_t s1 = std::string::npos;
        if(s0 != std::string::npos) {
          s1 = tok.find('/', s0 + 1);
          std::string tstr = tok.substr(s0 + 1, s1 - (s0 + 1));
          if(!tstr.empty()) {
            t = std::stoi(tstr);
            t = (t < 0) ? (int)tex.size() + t : t - 1;
          }
        }
        if(s1 != std::string::npos) {
          std::string nstr = tok.substr(s1 + 1);
          if(!nstr.empty()) {
            n = std::stoi(nstr);
            n = (n < 0) ? (int)nrm.size() + n : n - 1;
          }
        }
        ti.push_back(t);
        ni.push_back(n);
      }
      // fan-triangulate the polygon
      auto uv = [&](int idx) -> vec2f {
        return (idx >= 0 && idx < (int)tex.size()) ? tex[idx] : vec2f{0.0f, 0.0f};
      };
      auto nm = [&](int idx) -> vec3f {
        return (idx >= 0 && idx < (int)nrm.size()) ? nrm[idx] : vec3f{0.0f, 0.0f, 0.0f};
      };
      for(size_t k = 1; k + 1 < vi.size(); ++k) {
        int a = vi[0], b = vi[k], c = vi[k+1];
        if(a < 0 || b < 0 || c < 0) continue;
        if(a >= (int)pos.size() || b >= (int)pos.size() || c >= (int)pos.size()) continue;
        tris.push_back({ model_vert{pos[a], uv(ti[0]),   nm(ni[0])},
                         model_vert{pos[b], uv(ti[k]),   nm(ni[k])},
                         model_vert{pos[c], uv(ti[k+1]), nm(ni[k+1])} });
      }
    }
  }
  return tris;
}
