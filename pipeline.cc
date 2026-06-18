#include "pipeline.h"
#include <cmath>
#include <algorithm>

namespace {
  const float PI = 3.14159265358979f;
  const float DEPTH_MAX = 65535.0f;   // window depth quantized to 16 bits

  struct v3 { float x, y, z; };
  struct v4 { float x, y, z, w; };
  struct mat4 { float m[4][4]; };

  v3 sub(v3 a, v3 b)   { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
  v3 cross(v3 a, v3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
  float dot(v3 a, v3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
  v3 normalize(v3 a)   { float l = std::sqrt(dot(a,a)); return l > 0 ? v3{a.x/l,a.y/l,a.z/l} : a; }

  v4 mul(const mat4 &M, v4 v) {
    return {
      M.m[0][0]*v.x + M.m[0][1]*v.y + M.m[0][2]*v.z + M.m[0][3]*v.w,
      M.m[1][0]*v.x + M.m[1][1]*v.y + M.m[1][2]*v.z + M.m[1][3]*v.w,
      M.m[2][0]*v.x + M.m[2][1]*v.y + M.m[2][2]*v.z + M.m[2][3]*v.w,
      M.m[3][0]*v.x + M.m[3][1]*v.y + M.m[3][2]*v.z + M.m[3][3]*v.w };
  }
  mat4 mul(const mat4 &A, const mat4 &B) {
    mat4 R{};
    for(int i = 0; i < 4; i++)
      for(int j = 0; j < 4; j++)
	for(int k = 0; k < 4; k++)
	  R.m[i][j] += A.m[i][k] * B.m[k][j];
    return R;
  }
  mat4 identity()                  { mat4 R{}; for(int i=0;i<4;i++) R.m[i][i]=1; return R; }
  mat4 translate(float x,float y,float z){ mat4 R=identity(); R.m[0][3]=x; R.m[1][3]=y; R.m[2][3]=z; return R; }
  mat4 scale(float s)              { mat4 R=identity(); R.m[0][0]=R.m[1][1]=R.m[2][2]=s; return R; }
  mat4 rotateY(float a) {
    mat4 R=identity(); float c=std::cos(a), s=std::sin(a);
    R.m[0][0]=c; R.m[0][2]=s; R.m[2][0]=-s; R.m[2][2]=c; return R;
  }
  mat4 perspective(float fovy, float aspect, float n, float f) {
    mat4 R{}; float t = 1.0f / std::tan(fovy * 0.5f);
    R.m[0][0] = t/aspect;  R.m[1][1] = t;
    R.m[2][2] = (f+n)/(n-f);  R.m[2][3] = (2*f*n)/(n-f);
    R.m[3][2] = -1;
    return R;
  }
}

std::vector<screen_tri> project_mesh(const std::vector<model_tri> &mesh,
				     int width, int height, bool cull_backfaces,
				     float yaw_deg) {
  std::vector<screen_tri> out;
  if(mesh.empty()) return out;

  // bounding box -> center + uniform scale into roughly [-1,1]
  v3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
  for(const auto &t : mesh)
    for(int i = 0; i < 3; i++) {
      lo.x = std::min(lo.x, t[i].pos.x); hi.x = std::max(hi.x, t[i].pos.x);
      lo.y = std::min(lo.y, t[i].pos.y); hi.y = std::max(hi.y, t[i].pos.y);
      lo.z = std::min(lo.z, t[i].pos.z); hi.z = std::max(hi.z, t[i].pos.z);
    }
  v3 c{ (lo.x+hi.x)*0.5f, (lo.y+hi.y)*0.5f, (lo.z+hi.z)*0.5f };
  float ext = std::max(hi.x-lo.x, std::max(hi.y-lo.y, hi.z-lo.z));
  float fit = (ext > 0) ? 2.0f/ext : 1.0f;

  const float dist = 4.0f, nearp = 0.1f, farp = 100.0f;
  mat4 model = mul(rotateY(yaw_deg*PI/180.0f), mul(scale(fit), translate(-c.x,-c.y,-c.z)));
  mat4 MV    = mul(translate(0,0,-dist), model);
  mat4 P     = perspective(50.0f*PI/180.0f, (float)width/height, nearp, farp);

  const v3 L = normalize({0.3f, 0.5f, 1.0f});   // eye-space directional light
  const float ambient = 0.15f;
  const v3 base{230, 220, 210};

  for(const auto &tri : mesh) {
    // model -> eye space
    v4 e[3]; bool reject = false;
    for(int i = 0; i < 3; i++) {
      e[i] = mul(MV, v4{tri[i].pos.x, tri[i].pos.y, tri[i].pos.z, 1.0f});
      if(e[i].z > -nearp) reject = true;          // at/behind the near plane
    }
    if(reject) continue;

    v3 e0{e[0].x,e[0].y,e[0].z}, e1{e[1].x,e[1].y,e[1].z}, e2{e[2].x,e[2].y,e[2].z};
    v3 N = cross(sub(e1,e0), sub(e2,e0));

    // backface cull: e0 points from the camera (origin) to the face, so a
    // camera-facing normal opposes it (dot < 0); drop the rest.
    if(cull_backfaces && dot(N, e0) >= 0) continue;

    // project to screen + quantized depth; keep 1/w and uv per vertex for
    // perspective-correct texture interpolation.
    vertex3d sv[3];
    float invw[3];
    float uv[3][2];
    for(int i = 0; i < 3; i++) {
      v4 cl = mul(P, e[i]);
      float iw = 1.0f / cl.w;
      float sx = (cl.x*iw*0.5f + 0.5f) * width;
      float sy = (0.5f - cl.y*iw*0.5f) * height;   // flip y: screen origin upper-left
      float sz = (cl.z*iw*0.5f + 0.5f) * DEPTH_MAX;
      sv[i] = { (int32_t)std::lround(sx), (int32_t)std::lround(sy), (int32_t)std::lround(sz) };
      invw[i] = iw;
      uv[i][0] = tri[i].uv.u;
      uv[i][1] = tri[i].uv.v;
    }

    // Gouraud: light each vertex by its own (eye-space) normal.  Fall back to
    // the face normal if the OBJ vertex has none.  Two-sided per vertex.
    v3 nf_face = normalize(N);
    if(dot(nf_face, e0) > 0) nf_face = {-nf_face.x, -nf_face.y, -nf_face.z};
    v3 ev[3] = {e0, e1, e2};
    uint8_t col[3][3];
    for(int i = 0; i < 3; i++) {
      v4 n4 = mul(MV, v4{tri[i].nrm.x, tri[i].nrm.y, tri[i].nrm.z, 0.0f});
      v3 nv{n4.x, n4.y, n4.z};
      v3 nf = (nv.x!=0 || nv.y!=0 || nv.z!=0) ? normalize(nv) : nf_face;
      if(dot(nf, ev[i]) > 0) nf = {-nf.x, -nf.y, -nf.z};
      float ndl = std::max(0.0f, dot(nf, L));
      float k = ambient + (1.0f - ambient) * ndl;
      col[i][0] = (uint8_t)std::min(255.0f, base.x * k);
      col[i][1] = (uint8_t)std::min(255.0f, base.y * k);
      col[i][2] = (uint8_t)std::min(255.0f, base.z * k);
    }

    // RTL coverage needs positive screen area; fix winding, drop degenerate
    long area = (long)(sv[1].x-sv[0].x)*(sv[2].y-sv[0].y)
              - (long)(sv[2].x-sv[0].x)*(sv[1].y-sv[0].y);
    if(area == 0) continue;
    if(area < 0) {
      std::swap(sv[1], sv[2]);
      std::swap(invw[1], invw[2]);
      std::swap(uv[1][0], uv[2][0]);
      std::swap(uv[1][1], uv[2][1]);
      std::swap(col[1], col[2]);
    }

    screen_tri st;
    st.v[0] = sv[0]; st.v[1] = sv[1]; st.v[2] = sv[2];
    for(int i = 0; i < 3; i++) {
      st.invw[i]  = invw[i];
      st.uv[i][0] = uv[i][0];
      st.uv[i][1] = uv[i][1];
      st.col[i][0] = col[i][0];
      st.col[i][1] = col[i][1];
      st.col[i][2] = col[i][2];
    }
    out.push_back(st);
  }
  return out;
}
