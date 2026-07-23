/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "gl.h"

#include <float.h>
#include <math.h>

#define GL_SHADOW_DEFAULT_STRENGTH 1.0f
#define GL_SHADOW_DEFAULT_RECEIVER_BIAS 0.0008f
#define GL_SHADOW_CONE_RECEIVER_BIAS 0.00004f
#define GL_SHADOW_DEFAULT_MIN_BIAS 0.00005f
#define GL_SHADOW_CONE_MIN_BIAS 0.000005f
#define GL_SHADOW_CONE_NORMAL_OFFSET 0.05f
#define GL_SHADOW_CONE_DEPTH_BIAS 0.25f

typedef struct {
  int pages[SHADOW_FRONTEND_POINT_FACES];
  int page_count;
  shadow_view_type_t view_type;
} glShadowLightPages_t;

typedef struct {
  GLuint fbo;
  GLuint depth_array;
  GLuint moment_array;
  GLuint program;
  GLuint moment_program;
  GLuint alpha_program;
  GLuint alpha_moment_program;
  int resolution;
  int mip_levels;
  int max_active_page;
  bool resources_ok;
  bool frame_active;
  bool sun_active;
  bool reallocated_this_frame;
  bool reallocated_last_frame;
  bool moment_rendered;
  shadow_storage_family_t storage_family;
  int world_faces_considered;
  int world_faces_submitted;
  shadow_frontend_policy_t policy;
  glShadowLightPages_t lights[MAX_DLIGHTS];
} glShadowState_t;

static glShadowState_t gl_shadow;

typedef struct {
  vec3_t mins;
  vec3_t maxs;
} glShadowFaceBounds_t;

// World face bounds are immutable per map; caching them avoids walking every
// face's surfedges again for every shadow view every frame.
typedef struct {
  const bsp_t *bsp;
  uint32_t checksum;
  int numfaces;
  glShadowFaceBounds_t *bounds;
} glShadowWorldCache_t;

static glShadowWorldCache_t gl_shadow_world_cache;

static void GL_Shadow_FreeWorldCache(void) {
  Z_Free(gl_shadow_world_cache.bounds);
  memset(&gl_shadow_world_cache, 0, sizeof(gl_shadow_world_cache));
}

static float GL_Shadow_ViewReceiverBias(const shadow_view_desc_t *view) {
  float bias_scale = max(gl_shadow.policy.bias_scale, 0.0f);
  if (bias_scale <= 0.0f) {
    return 0.0f;
  }

  bool cone = view && view->view_type == SHADOW_VIEW_CONE;
  float base = cone ? GL_SHADOW_CONE_RECEIVER_BIAS
                    : GL_SHADOW_DEFAULT_RECEIVER_BIAS;
  float min_bias = cone ? GL_SHADOW_CONE_MIN_BIAS
                        : GL_SHADOW_DEFAULT_MIN_BIAS;
  return max(min_bias, bias_scale * base);
}

static float GL_Shadow_ViewNormalOffset(const shadow_view_desc_t *view) {
  float normal_offset = max(gl_shadow.policy.normal_offset, 0.0f);
  if (view && view->view_type == SHADOW_VIEW_CONE) {
    normal_offset = min(normal_offset, GL_SHADOW_CONE_NORMAL_OFFSET);
  }
  return normal_offset;
}

static float GL_Shadow_ViewSlopeBias(const shadow_view_desc_t *view) {
  float slope_bias = max(gl_shadow.policy.slope_bias, 0.0f);
  if (view && view->view_type == SHADOW_VIEW_CONE) {
    slope_bias = min(slope_bias, GL_SHADOW_CONE_DEPTH_BIAS);
  }
  return slope_bias;
}

static float GL_Shadow_ViewConstantBias(const shadow_view_desc_t *view) {
  float constant_bias = max(gl_shadow.policy.bias_scale, 0.0f);
  if (view && view->view_type == SHADOW_VIEW_CONE) {
    constant_bias = min(constant_bias, GL_SHADOW_CONE_DEPTH_BIAS);
  }
  return constant_bias;
}

static void GL_Shadow_InvalidateState(void) {
  gls.state_bits = ~(glStateBits_t)0;
  gls.array_bits = GLA_NONE;
  gls.currentva = VA_NONE;
  gls.currentmodelmatrix = NULL;
  gls.currentviewmatrix = NULL;
  memset(gls.texnums, 0, sizeof(gls.texnums));
}

static void GL_Shadow_Ortho(GLfloat xmin, GLfloat xmax, GLfloat ymin,
                            GLfloat ymax, GLfloat znear, GLfloat zfar,
                            mat4_t matrix) {
  GLfloat width = xmax - xmin;
  GLfloat height = ymax - ymin;
  GLfloat depth = zfar - znear;

  matrix[0] = 2 / width;
  matrix[4] = 0;
  matrix[8] = 0;
  matrix[12] = -(xmax + xmin) / width;

  matrix[1] = 0;
  matrix[5] = 2 / height;
  matrix[9] = 0;
  matrix[13] = -(ymax + ymin) / height;

  matrix[2] = 0;
  matrix[6] = 0;
  matrix[10] = -2 / depth;
  matrix[14] = -(zfar + znear) / depth;

  matrix[3] = 0;
  matrix[7] = 0;
  matrix[11] = 0;
  matrix[15] = 1;
}

static void GL_Shadow_MultiplyMatrix(const mat4_t a, const mat4_t b,
                                     mat4_t out) {
  mat4_t tmp;
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      tmp[col * 4 + row] =
          b[col * 4 + 0] * a[0 * 4 + row] +
          b[col * 4 + 1] * a[1 * 4 + row] +
          b[col * 4 + 2] * a[2 * 4 + row] +
          b[col * 4 + 3] * a[3 * 4 + row];
    }
  }
  memcpy(out, tmp, sizeof(tmp));
}

static bool GL_Shadow_BindUniformBlock(GLuint program, const char *name,
                                       GLuint binding) {
  GLuint index = qglGetUniformBlockIndex(program, name);
  if (index == (GLuint)-1) {
    Com_EPrintf("OpenGL shadow shader missing uniform block %s\n", name);
    return false;
  }
  qglUniformBlockBinding(program, index, binding);
  return true;
}

static GLuint GL_Shadow_CompileShader(GLenum type, const char *source) {
  GLuint shader = qglCreateShader(type);
  const GLchar *sources[] = {source};
  qglShaderSource(shader, 1, sources, NULL);
  qglCompileShader(shader);

  GLint status = 0;
  qglGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    char buffer[MAX_STRING_CHARS];
    buffer[0] = 0;
    qglGetShaderInfoLog(shader, sizeof(buffer), NULL, buffer);
    if (buffer[0]) {
      Com_Printf("%s", buffer);
    }
    qglDeleteShader(shader);
    return 0;
  }
  return shader;
}

static const char *GL_Shadow_Header(void) {
  if (gl_config.ver_es) {
    return "#version 300 es\nprecision mediump float;\n";
  }
  if (gl_config.ver_sl >= QGL_VER(1, 40)) {
    return "#version 140\n";
  }
  return "#version 130\n#extension GL_ARB_uniform_buffer_object : require\n";
}

static bool GL_Shadow_CreateProgram(void) {
  if (gl_shadow.program) {
    return true;
  }

  char vertex_source[2048];
  Q_snprintf(vertex_source, sizeof(vertex_source),
             "%s"
             "layout(std140) uniform Uniforms {\n"
             "mat4 m_model; mat4 m_view; mat4 m_proj;\n"
             "};\n"
             "in vec3 a_pos;\n"
             "void main() {\n"
             "  gl_Position = m_proj * m_view * m_model * vec4(a_pos, 1.0);\n"
             "}\n",
             GL_Shadow_Header());

  char fragment_source[512];
  Q_snprintf(fragment_source, sizeof(fragment_source),
             "%s"
             "void main() { }\n",
             GL_Shadow_Header());

  GLuint vertex_shader = GL_Shadow_CompileShader(GL_VERTEX_SHADER, vertex_source);
  if (!vertex_shader) {
    return false;
  }
  GLuint fragment_shader = GL_Shadow_CompileShader(GL_FRAGMENT_SHADER,
                                                   fragment_source);
  if (!fragment_shader) {
    qglDeleteShader(vertex_shader);
    return false;
  }

  GLuint program = qglCreateProgram();
  qglAttachShader(program, vertex_shader);
  qglAttachShader(program, fragment_shader);
  qglBindAttribLocation(program, VERT_ATTR_POS, "a_pos");
  qglLinkProgram(program);
  qglDeleteShader(vertex_shader);
  qglDeleteShader(fragment_shader);

  GLint status = 0;
  qglGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    char buffer[MAX_STRING_CHARS];
    buffer[0] = 0;
    qglGetProgramInfoLog(program, sizeof(buffer), NULL, buffer);
    if (buffer[0]) {
      Com_Printf("%s", buffer);
    }
    qglDeleteProgram(program);
    return false;
  }

  if (!GL_Shadow_BindUniformBlock(program, "Uniforms", UBO_UNIFORMS)) {
    qglDeleteProgram(program);
    return false;
  }

  gl_shadow.program = program;
  return true;
}

static bool GL_Shadow_CreateMomentProgram(void) {
  if (gl_shadow.moment_program) {
    return true;
  }

  char vertex_source[2048];
  Q_snprintf(vertex_source, sizeof(vertex_source),
             "%s"
             "layout(std140) uniform Uniforms {\n"
             "mat4 m_model; mat4 m_view; mat4 m_proj;\n"
             "};\n"
             "in vec3 a_pos;\n"
             "void main() {\n"
             "  gl_Position = m_proj * m_view * m_model * vec4(a_pos, 1.0);\n"
             "}\n",
             GL_Shadow_Header());

  char fragment_source[1024];
  Q_snprintf(fragment_source, sizeof(fragment_source),
             "%s"
             "uniform int u_shadow_filter;\n"
             "out vec4 o_moment;\n"
             "void main() {\n"
             "  float d = clamp(gl_FragCoord.z, 0.0, 1.0);\n"
             "  if (u_shadow_filter == %d) {\n"
             "    const float evsm_exponent = " GL_SHADOW_EVSM_EXPONENT_GLSL ";\n"
             "    float w = exp(min(evsm_exponent * d, evsm_exponent));\n"
             "    o_moment = vec4(w, w * w, 0.0, 1.0);\n"
             "  } else {\n"
             "    o_moment = vec4(d, d * d, 0.0, 1.0);\n"
             "  }\n"
             "}\n",
             GL_Shadow_Header(), SHADOW_FILTER_EVSM);

  GLuint vertex_shader = GL_Shadow_CompileShader(GL_VERTEX_SHADER, vertex_source);
  if (!vertex_shader) {
    return false;
  }
  GLuint fragment_shader = GL_Shadow_CompileShader(GL_FRAGMENT_SHADER,
                                                   fragment_source);
  if (!fragment_shader) {
    qglDeleteShader(vertex_shader);
    return false;
  }

  GLuint program = qglCreateProgram();
  qglAttachShader(program, vertex_shader);
  qglAttachShader(program, fragment_shader);
  qglBindAttribLocation(program, VERT_ATTR_POS, "a_pos");
  if (!gl_config.ver_es) {
    qglBindFragDataLocation(program, 0, "o_moment");
  }
  qglLinkProgram(program);
  qglDeleteShader(vertex_shader);
  qglDeleteShader(fragment_shader);

  GLint status = 0;
  qglGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    char buffer[MAX_STRING_CHARS];
    buffer[0] = 0;
    qglGetProgramInfoLog(program, sizeof(buffer), NULL, buffer);
    if (buffer[0]) {
      Com_Printf("%s", buffer);
    }
    qglDeleteProgram(program);
    return false;
  }

  if (!GL_Shadow_BindUniformBlock(program, "Uniforms", UBO_UNIFORMS)) {
    qglDeleteProgram(program);
    return false;
  }

  gl_shadow.moment_program = program;
  return true;
}

static bool GL_Shadow_CreateAlphaProgram(bool moment) {
  GLuint *output = moment ? &gl_shadow.alpha_moment_program
                          : &gl_shadow.alpha_program;
  if (*output) {
    return true;
  }

  char vertex_source[2048];
  Q_snprintf(vertex_source, sizeof(vertex_source),
             "%s"
             "layout(std140) uniform Uniforms {\n"
             "mat4 m_model; mat4 m_view; mat4 m_proj;\n"
             "};\n"
             "in vec3 a_pos;\n"
             "in vec2 a_tc;\n"
             "out vec2 v_tc;\n"
             "void main() {\n"
             "  gl_Position = m_proj * m_view * m_model * vec4(a_pos, 1.0);\n"
             "  v_tc = a_tc;\n"
             "}\n",
             GL_Shadow_Header());

  char fragment_source[1280];
  if (moment) {
    Q_snprintf(fragment_source, sizeof(fragment_source),
               "%s"
               "uniform sampler2D u_diffuse;\n"
               "uniform int u_shadow_filter;\n"
               "in vec2 v_tc;\n"
               "out vec4 o_moment;\n"
               "void main() {\n"
               "  if (texture(u_diffuse, v_tc).a <= 0.666) discard;\n"
               "  float d = clamp(gl_FragCoord.z, 0.0, 1.0);\n"
               "  if (u_shadow_filter == %d) {\n"
               "    const float evsm_exponent = " GL_SHADOW_EVSM_EXPONENT_GLSL ";\n"
               "    float w = exp(min(evsm_exponent * d, evsm_exponent));\n"
               "    o_moment = vec4(w, w * w, 0.0, 1.0);\n"
               "  } else {\n"
               "    o_moment = vec4(d, d * d, 0.0, 1.0);\n"
               "  }\n"
               "}\n",
               GL_Shadow_Header(), SHADOW_FILTER_EVSM);
  } else {
    Q_snprintf(fragment_source, sizeof(fragment_source),
               "%s"
               "uniform sampler2D u_diffuse;\n"
               "in vec2 v_tc;\n"
               "void main() {\n"
               "  if (texture(u_diffuse, v_tc).a <= 0.666) discard;\n"
               "}\n",
               GL_Shadow_Header());
  }

  GLuint vertex_shader = GL_Shadow_CompileShader(GL_VERTEX_SHADER, vertex_source);
  if (!vertex_shader) {
    return false;
  }
  GLuint fragment_shader = GL_Shadow_CompileShader(GL_FRAGMENT_SHADER,
                                                   fragment_source);
  if (!fragment_shader) {
    qglDeleteShader(vertex_shader);
    return false;
  }

  GLuint program = qglCreateProgram();
  qglAttachShader(program, vertex_shader);
  qglAttachShader(program, fragment_shader);
  qglBindAttribLocation(program, VERT_ATTR_POS, "a_pos");
  qglBindAttribLocation(program, VERT_ATTR_TC, "a_tc");
  if (moment && !gl_config.ver_es) {
    qglBindFragDataLocation(program, 0, "o_moment");
  }
  qglLinkProgram(program);
  qglDeleteShader(vertex_shader);
  qglDeleteShader(fragment_shader);

  GLint status = 0;
  qglGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    char buffer[MAX_STRING_CHARS];
    buffer[0] = 0;
    qglGetProgramInfoLog(program, sizeof(buffer), NULL, buffer);
    if (buffer[0]) {
      Com_Printf("%s", buffer);
    }
    qglDeleteProgram(program);
    return false;
  }

  if (!GL_Shadow_BindUniformBlock(program, "Uniforms", UBO_UNIFORMS)) {
    qglDeleteProgram(program);
    return false;
  }

  *output = program;
  return true;
}

static int GL_Shadow_ClampResolution(int requested) {
  requested = (int)Q_clipf((float)requested, 64.0f,
                           (float)GL_SHADOW_MAX_RESOLUTION);
  int resolution = 64;
  while (resolution < requested) {
    resolution <<= 1;
  }
  return min(resolution, GL_SHADOW_MAX_RESOLUTION);
}

static void GL_Shadow_DestroyResources(void) {
  if (gl_shadow.moment_array) {
    qglDeleteTextures(1, &gl_shadow.moment_array);
    gl_shadow.moment_array = 0;
  }
  if (gl_shadow.depth_array) {
    qglDeleteTextures(1, &gl_shadow.depth_array);
    gl_shadow.depth_array = 0;
  }
  if (gl_shadow.fbo) {
    qglDeleteFramebuffers(1, &gl_shadow.fbo);
    gl_shadow.fbo = 0;
  }
  gl_shadow.resolution = 0;
  gl_shadow.mip_levels = 1;
  gl_shadow.resources_ok = false;
}

static int GL_Shadow_MipLevels(int resolution) {
  int levels = 1;
  while (resolution > 1) {
    resolution >>= 1;
    levels++;
  }
  return levels;
}

static bool GL_Shadow_EnsureResources(int resolution,
                                      shadow_storage_family_t storage,
                                      bool *allocated) {
  if (allocated) {
    *allocated = false;
  }

  if (!gl_static.use_shaders || !qglTexImage3D || !qglFramebufferTextureLayer ||
      !qglDrawBuffers || !qglGenFramebuffers) {
    return false;
  }
  if (!GL_Shadow_CreateProgram()) {
    return false;
  }
  if (!GL_Shadow_CreateAlphaProgram(false)) {
    return false;
  }
  if (storage == SHADOW_STORAGE_MOMENT &&
      (!GL_Shadow_CreateMomentProgram() ||
       !GL_Shadow_CreateAlphaProgram(true))) {
    return false;
  }

  resolution = GL_Shadow_ClampResolution(resolution);
  if (gl_shadow.resources_ok && gl_shadow.resolution >= resolution &&
      gl_shadow.storage_family == storage) {
    return true;
  }

  GL_Shadow_DestroyResources();
  gl_shadow.storage_family = storage;

  qglGenTextures(1, &gl_shadow.depth_array);
  qglBindTexture(GL_TEXTURE_2D_ARRAY, gl_shadow.depth_array);
  qglTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, resolution,
                resolution, GL_SHADOW_MAX_PAGES, 0, GL_DEPTH_COMPONENT,
                GL_UNSIGNED_INT, NULL);
  // Receiver shaders fetch raw depth and perform their own hard, PCF, and
  // PCSS comparisons. Interpolating depth before those comparisons produces
  // false visibility at discontinuities, so raw-depth taps must be nearest.
  qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);

  if (storage == SHADOW_STORAGE_MOMENT) {
    gl_shadow.mip_levels = GL_Shadow_MipLevels(resolution);
    qglGenTextures(1, &gl_shadow.moment_array);
    qglBindTexture(GL_TEXTURE_2D_ARRAY, gl_shadow.moment_array);
    int level_resolution = resolution;
    for (int level = 0; level < gl_shadow.mip_levels; level++) {
      qglTexImage3D(GL_TEXTURE_2D_ARRAY, level, GL_RGBA16F,
                    level_resolution, level_resolution, GL_SHADOW_MAX_PAGES,
                    0, GL_RGBA, GL_HALF_FLOAT, NULL);
      level_resolution = max(level_resolution >> 1, 1);
    }
    qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                     GL_LINEAR_MIPMAP_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  qglGenFramebuffers(1, &gl_shadow.fbo);
  qglBindFramebuffer(GL_FRAMEBUFFER, gl_shadow.fbo);
  GLenum draw_buffer = storage == SHADOW_STORAGE_MOMENT
      ? GL_COLOR_ATTACHMENT0
      : GL_NONE;
  qglDrawBuffers(1, &draw_buffer);
  if (qglReadBuffer) {
    qglReadBuffer(GL_NONE);
  }
  qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             gl_shadow.depth_array, 0, 0);
  if (storage == SHADOW_STORAGE_MOMENT) {
    qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               gl_shadow.moment_array, 0, 0);
  }

  GLenum status = qglCheckFramebufferStatus(GL_FRAMEBUFFER);
  qglBindFramebuffer(GL_FRAMEBUFFER, glr.framebuffer_bound ? FBO_SCENE : 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    Com_EPrintf("OpenGL shadow framebuffer incomplete: 0x%x\n", status);
    GL_Shadow_DestroyResources();
    GL_Shadow_InvalidateState();
    return false;
  }

  gl_shadow.resolution = resolution;
  gl_shadow.resources_ok = true;
  gl_shadow.reallocated_this_frame = true;
  if (allocated) {
    *allocated = true;
  }
  GL_Shadow_InvalidateState();
  return true;
}

static void GL_Shadow_RecordLightPage(const shadow_view_desc_t *view) {
  if (view->view_type == SHADOW_VIEW_SUN_CASCADE) {
    if (gls.u_shadows.sun[0] < 0.0f ||
        (float)view->page.index < gls.u_shadows.sun[0]) {
      gls.u_shadows.sun[0] = (float)view->page.index;
    }
    gls.u_shadows.sun[1] += 1.0f;
    gls.u_shadows.sun[2] = gls.u_shadows.pages[view->page.index].params[2];
    gls.u_shadows.sun[3] = GL_SHADOW_DEFAULT_STRENGTH;
    return;
  }

  if (view->light_index >= MAX_DLIGHTS) {
    return;
  }

  glShadowLightPages_t *light = &gl_shadow.lights[view->light_index];
  light->view_type = view->view_type;
  if (view->view_type == SHADOW_VIEW_POINT_FACE &&
      view->face >= 0 && view->face < SHADOW_FRONTEND_POINT_FACES) {
    light->pages[view->face] = (int)view->page.index;
  } else {
    light->pages[0] = (int)view->page.index;
  }

  int count = 0;
  for (int i = 0; i < SHADOW_FRONTEND_POINT_FACES; i++) {
    if (light->pages[i] >= 0) {
      count++;
    }
  }
  light->page_count = count;
}

static void GL_Shadow_RegisterView(const shadow_view_desc_t *view) {
  if (!view || view->page.index >= GL_SHADOW_MAX_PAGES ||
      !gl_shadow.resources_ok) {
    return;
  }

  mat4_t view_matrix;
  mat4_t proj_matrix;
  Matrix_FromOriginAxis(view->origin, view->axis, view_matrix);

  if (view->view_type == SHADOW_VIEW_SUN_CASCADE) {
    float half = max(view->ortho_size * 0.5f, 64.0f);
    float near_z = max(view->near_z, 0.0f);
    float far_z = max(view->far_z, near_z + 1.0f);
    GL_Shadow_Ortho(-half, half, -half, half, -far_z, -near_z,
                    proj_matrix);
  } else {
    float near_z = max(view->near_z, 1.0f);
    float far_z = max(view->far_z, near_z + 1.0f);
    Matrix_Frustum(view->fov_x, view->fov_y, 1.0f, near_z, far_z,
                   proj_matrix);
  }

  glShadowPage_t *page = &gls.u_shadows.pages[view->page.index];
  GL_Shadow_MultiplyMatrix(proj_matrix, view_matrix, page->matrix);
  page->params[0] = (float)view->filter_family;
  page->params[1] = 1.0f / (float)gl_shadow.resolution;
  page->params[2] = GL_Shadow_ViewReceiverBias(view);
  page->params[3] = GL_Shadow_ViewNormalOffset(view);

  gl_shadow.max_active_page = max(gl_shadow.max_active_page,
                                  (int)view->page.index);
  GL_Shadow_RecordLightPage(view);
}

static inline const mvertex_t *GL_Shadow_SurfEdgeVertex(const bsp_t *bsp,
                                                        const msurfedge_t *edge) {
  if (!bsp || !edge || edge->edge >= (uint32_t)bsp->numedges) {
    return NULL;
  }
  const medge_t *src_edge = &bsp->edges[edge->edge];
  uint32_t vertex = src_edge->v[edge->vert ? 1 : 0];
  if (vertex >= (uint32_t)bsp->numvertices) {
    return NULL;
  }
  return &bsp->vertices[vertex];
}

static void GL_Shadow_AddPointToBounds(const vec3_t point,
                                       vec3_t mins,
                                       vec3_t maxs) {
  for (int i = 0; i < 3; i++) {
    if (point[i] < mins[i]) {
      mins[i] = point[i];
    }
    if (point[i] > maxs[i]) {
      maxs[i] = point[i];
    }
  }
}

static void GL_Shadow_FlushPositions(void) {
  if (!tess.numverts) {
    return;
  }

  GL_BindArrays(VA_OCCLUDE);
  GL_ArrayBits(GLA_VERTEX);
  GL_LoadUniforms();
  GL_LockArrays(tess.numverts);
  qglDrawArrays(GL_TRIANGLES, 0, tess.numverts);
  GL_UnlockArrays();
  c.trisDrawn += tess.numverts / 3;
  c.batchesDrawn++;
  tess.numverts = 0;
}

static void GL_Shadow_AddPosition(const vec3_t pos) {
  if (tess.numverts >= TESS_MAX_VERTICES) {
    GL_Shadow_FlushPositions();
  }
  VectorCopy(pos, tess.vertices + tess.numverts * 3);
  tess.numverts++;
}

static void GL_Shadow_AddTriangle(const vec3_t a, const vec3_t b,
                                  const vec3_t cpos) {
  if (tess.numverts + 3 > TESS_MAX_VERTICES) {
    GL_Shadow_FlushPositions();
  }
  GL_Shadow_AddPosition(a);
  GL_Shadow_AddPosition(b);
  GL_Shadow_AddPosition(cpos);
}

static void GL_Shadow_SetMomentFilter(GLuint program,
                                      const shadow_view_desc_t *view) {
  if (!program || !view || view->storage_family != SHADOW_STORAGE_MOMENT) {
    return;
  }
  GLint loc = qglGetUniformLocation(program, "u_shadow_filter");
  if (loc >= 0) {
    qglUniform1i(loc, (GLint)view->filter_family);
  }
}

static void GL_Shadow_UseOpaqueProgram(const shadow_view_desc_t *view) {
  GLuint program = view->storage_family == SHADOW_STORAGE_MOMENT
      ? gl_shadow.moment_program
      : gl_shadow.program;
  qglUseProgram(program);
  GL_Shadow_SetMomentFilter(program, view);
}

static void GL_Shadow_FlushAlpha(void) {
  if (!tess.numverts) {
    return;
  }

  GL_BindArrays(VA_SHADOW_ALPHA);
  GL_ArrayBits(GLA_VERTEX | GLA_TC);
  GL_LoadUniforms();
  GL_LockArrays(tess.numverts);
  qglDrawArrays(GL_TRIANGLES, 0, tess.numverts);
  GL_UnlockArrays();
  c.trisDrawn += tess.numverts / 3;
  c.batchesDrawn++;
  tess.numverts = 0;
}

static void GL_Shadow_AddAlphaPosition(const vec3_t pos, const vec2_t tc) {
  if (tess.numverts >= TESS_MAX_VERTICES) {
    GL_Shadow_FlushAlpha();
  }
  GLfloat *vertex = tess.vertices + tess.numverts * 5;
  VectorCopy(pos, vertex);
  vertex[3] = tc[0];
  vertex[4] = tc[1];
  tess.numverts++;
}

static void GL_Shadow_AddAlphaTriangle(const vec3_t a, const vec3_t b,
                                       const vec3_t cpos, const mface_t *face,
                                       const vec3_t texture_points[3]) {
  if (!face || !face->texinfo || !face->texinfo->image || !texture_points) {
    return;
  }

  const image_t *image = face->texinfo->image;
  if (image->width < 1 || image->height < 1) {
    return;
  }
  const vec_t *positions[3] = { a, b, cpos };
  for (int i = 0; i < 3; i++) {
    vec2_t tc;
    tc[0] = (DotProduct(texture_points[i], face->texinfo->axis[0]) +
             face->texinfo->offset[0]) / (float)image->width;
    tc[1] = (DotProduct(texture_points[i], face->texinfo->axis[1]) +
             face->texinfo->offset[1]) / (float)image->height;
    GL_Shadow_AddAlphaPosition(positions[i], tc);
  }
}

static bool GL_Shadow_BeginAlphaFace(const mface_t *face,
                                     const shadow_view_desc_t *view) {
  if (!face || !face->texinfo || !face->texinfo->image ||
      !face->texinfo->image->texnum) {
    return false;
  }

  GL_Shadow_FlushPositions();
  GLuint program = view->storage_family == SHADOW_STORAGE_MOMENT
      ? gl_shadow.alpha_moment_program
      : gl_shadow.alpha_program;
  if (!program) {
    return false;
  }
  qglUseProgram(program);
  GL_Shadow_SetMomentFilter(program, view);
  GLint loc = qglGetUniformLocation(program, "u_diffuse");
  if (loc >= 0) {
    qglUniform1i(loc, TMU_TEXTURE);
  }
  GL_BindTexture(TMU_TEXTURE, face->texinfo->image->texnum);
  tess.numverts = 0;
  return true;
}

static void GL_Shadow_EndAlphaFace(const shadow_view_desc_t *view) {
  GL_Shadow_FlushAlpha();
  GL_Shadow_UseOpaqueProgram(view);
}

typedef struct {
  vec3_t origin;
  vec3_t axis[3];
} glShadowCasterTransform_t;

static void GL_Shadow_BuildCasterTransform(const entity_t *ent,
                                           glShadowCasterTransform_t *out) {
  memset(out, 0, sizeof(*out));
  VectorCopy(ent->origin, out->origin);
  if (VectorEmpty(ent->angles)) {
    VectorSet(out->axis[0], 1.0f, 0.0f, 0.0f);
    VectorSet(out->axis[1], 0.0f, 1.0f, 0.0f);
    VectorSet(out->axis[2], 0.0f, 0.0f, 1.0f);
  } else {
    AnglesToAxis(ent->angles, out->axis);
  }

  for (int i = 0; i < 3; i++) {
    float scale = ent->scale[i] != 0.0f ? ent->scale[i] : 1.0f;
    VectorScale(out->axis[i], scale, out->axis[i]);
  }
}

static void GL_Shadow_TransformCasterPoint(const glShadowCasterTransform_t *transform,
                                           const vec3_t local,
                                           vec3_t out) {
  VectorCopy(transform->origin, out);
  VectorMA(out, local[0], transform->axis[0], out);
  VectorMA(out, local[1], transform->axis[1], out);
  VectorMA(out, local[2], transform->axis[2], out);
}

static bool GL_Shadow_ResolveAliasFrames(const entity_t *ent,
                                         int numframes,
                                         unsigned *frame,
                                         unsigned *oldframe,
                                         float *backlerp,
                                         float *frontlerp) {
  if (!ent || numframes <= 0 || !frame || !oldframe || !backlerp ||
      !frontlerp) {
    return false;
  }

  unsigned new_frame = ent->frame;
  unsigned old_frame = ent->oldframe;
  if (glr.fd.extended) {
    new_frame %= (unsigned)numframes;
    old_frame %= (unsigned)numframes;
  } else {
    if (new_frame >= (unsigned)numframes) {
      new_frame = 0;
    }
    if (old_frame >= (unsigned)numframes) {
      old_frame = 0;
    }
  }

  float back = Q_clipf(ent->backlerp, 0.0f, 1.0f);
  if (back == 0.0f) {
    old_frame = new_frame;
  }

  *frame = new_frame;
  *oldframe = old_frame;
  *backlerp = back;
  *frontlerp = 1.0f - back;
  return true;
}

static const void *GL_Shadow_ModelVertexData(const model_t *model,
                                             const void *ptr) {
  if (model->shadow_vertex_data) {
    return (const byte *)model->shadow_vertex_data + (uintptr_t)ptr;
  }
  return ptr;
}

static const void *GL_Shadow_ModelIndexData(const model_t *model,
                                            const void *ptr) {
  if (model->shadow_index_data) {
    return (const byte *)model->shadow_index_data + (uintptr_t)ptr;
  }
  return ptr;
}

static void GL_Shadow_AliasVertexLocal(const model_t *model,
                                       const maliasvert_t *verts,
                                       int numverts,
                                       int vertex_index,
                                       unsigned frame,
                                       unsigned oldframe,
                                       float backlerp,
                                       float frontlerp,
                                       vec3_t out) {
  if (frame == oldframe) {
    const maliasframe_t *new_frame = &model->frames[frame];
    const maliasvert_t *vert = &verts[frame * numverts + vertex_index];
    out[0] = vert->pos[0] * new_frame->scale[0] + new_frame->translate[0];
    out[1] = vert->pos[1] * new_frame->scale[1] + new_frame->translate[1];
    out[2] = vert->pos[2] * new_frame->scale[2] + new_frame->translate[2];
    return;
  }

  const maliasframe_t *new_frame = &model->frames[frame];
  const maliasframe_t *old_frame = &model->frames[oldframe];
  const maliasvert_t *new_vert = &verts[frame * numverts + vertex_index];
  const maliasvert_t *old_vert = &verts[oldframe * numverts + vertex_index];
  out[0] = (old_vert->pos[0] * old_frame->scale[0] + old_frame->translate[0]) * backlerp +
           (new_vert->pos[0] * new_frame->scale[0] + new_frame->translate[0]) * frontlerp;
  out[1] = (old_vert->pos[1] * old_frame->scale[1] + old_frame->translate[1]) * backlerp +
           (new_vert->pos[1] * new_frame->scale[1] + new_frame->translate[1]) * frontlerp;
  out[2] = (old_vert->pos[2] * old_frame->scale[2] + old_frame->translate[2]) * backlerp +
           (new_vert->pos[2] * new_frame->scale[2] + new_frame->translate[2]) * frontlerp;
}

#if USE_MD5
static void GL_Shadow_MD5VertexLocal(const md5_vertex_t *vert,
                                     const md5_weight_t *weights,
                                     const uint8_t *jointnums,
                                     const md5_joint_t *skeleton,
                                     vec3_t out) {
  VectorClear(out);
  for (int i = 0; i < vert->count; i++) {
    const md5_weight_t *weight = &weights[vert->start + i];
    const md5_joint_t *joint = &skeleton[jointnums[vert->start + i]];
    vec3_t rotated;
    vec3_t weighted;
    VectorRotate(weight->pos, joint->axis, rotated);
    VectorMA(joint->pos, joint->scale, rotated, weighted);
    VectorMA(out, weight->bias, weighted, out);
  }
}

static const md5_joint_t *GL_Shadow_LerpSkeleton(const md5_model_t *model,
                                                unsigned oldframe,
                                                unsigned frame,
                                                float backlerp,
                                                float frontlerp,
                                                md5_joint_t *scratch) {
  if (!model || !scratch || !model->skeleton_frames ||
      model->num_joints <= 0 || model->num_frames <= 0) {
    return NULL;
  }

  unsigned old_skel_frame = oldframe % (unsigned)model->num_frames;
  unsigned new_skel_frame = frame % (unsigned)model->num_frames;
  if (old_skel_frame == new_skel_frame) {
    return &model->skeleton_frames[new_skel_frame * model->num_joints];
  }

  const md5_joint_t *old_skel =
      &model->skeleton_frames[old_skel_frame * model->num_joints];
  const md5_joint_t *new_skel =
      &model->skeleton_frames[new_skel_frame * model->num_joints];
  for (int i = 0; i < model->num_joints; i++) {
    scratch[i].scale = new_skel[i].scale;
    LerpVector2(old_skel[i].pos, new_skel[i].pos, backlerp, frontlerp,
                scratch[i].pos);
    Quat_SLerp(old_skel[i].orient, new_skel[i].orient, backlerp, frontlerp,
               scratch[i].orient);
    Quat_ToAxis(scratch[i].orient, scratch[i].axis);
  }
  return scratch;
}

static bool GL_Shadow_DrawSkeletalCaster(const entity_t *ent,
                                         const model_t *alias_model,
                                         const glShadowCasterTransform_t *transform,
                                         unsigned frame,
                                         unsigned oldframe,
                                         float backlerp,
                                         float frontlerp) {
  const md5_model_t *skel_model = alias_model->skeleton;
  if (!skel_model || !skel_model->meshes || skel_model->num_joints <= 0 ||
      skel_model->num_joints > MD5_MAX_JOINTS || skel_model->num_frames <= 0) {
    return false;
  }
  if (!gl_md5_use || !gl_md5_use->integer) {
    return false;
  }
  if (!(ent->flags & RF_NO_LOD) && gl_md5_distance &&
      gl_md5_distance->value > 0.0f &&
      Distance(ent->origin, glr.fd.vieworg) > gl_md5_distance->value) {
    return false;
  }

  md5_joint_t scratch[MD5_MAX_JOINTS];
  const md5_joint_t *skeleton =
      GL_Shadow_LerpSkeleton(skel_model, oldframe, frame, backlerp, frontlerp,
                             scratch);
  if (!skeleton) {
    return false;
  }

  for (int mesh_index = 0; mesh_index < skel_model->num_meshes; mesh_index++) {
    const md5_mesh_t *mesh = &skel_model->meshes[mesh_index];
    if (!mesh->vertices || !mesh->indices || !mesh->weights ||
        !mesh->jointnums || mesh->num_indices < 3) {
      continue;
    }
    const md5_vertex_t *vertices =
        GL_Shadow_ModelVertexData(alias_model, mesh->vertices);
    const md5_weight_t *weights =
        GL_Shadow_ModelVertexData(alias_model, mesh->weights);
    const uint8_t *jointnums =
        GL_Shadow_ModelVertexData(alias_model, mesh->jointnums);
    const uint16_t *indices =
        GL_Shadow_ModelIndexData(alias_model, mesh->indices);
    for (int tri = 0; tri + 2 < mesh->num_indices; tri += 3) {
      vec3_t world[3];
      bool valid = true;
      for (int corner = 0; corner < 3; corner++) {
        int vertex_index = indices[tri + corner];
        if (vertex_index < 0 || vertex_index >= mesh->num_verts) {
          valid = false;
          break;
        }
        vec3_t local;
        GL_Shadow_MD5VertexLocal(&vertices[vertex_index], weights, jointnums,
                                 skeleton, local);
        GL_Shadow_TransformCasterPoint(transform, local, world[corner]);
      }
      if (valid) {
        GL_Shadow_AddTriangle(world[0], world[1], world[2]);
      }
    }
  }
  return true;
}
#endif

static bool GL_Shadow_DrawAliasCaster(const shadow_caster_t *caster) {
  const entity_t *ent = &caster->entity;
  const model_t *model = MOD_ForHandle(caster->model);
  if (!model || model->type != MOD_ALIAS || !model->frames ||
      model->numframes <= 0) {
    return false;
  }

  unsigned frame = 0;
  unsigned oldframe = 0;
  float backlerp = 0.0f;
  float frontlerp = 1.0f;
  if (!GL_Shadow_ResolveAliasFrames(ent, model->numframes, &frame, &oldframe,
                                    &backlerp, &frontlerp)) {
    return false;
  }

  glShadowCasterTransform_t transform;
  GL_Shadow_BuildCasterTransform(ent, &transform);

#if USE_MD5
  if (model->skeleton &&
      GL_Shadow_DrawSkeletalCaster(ent, model, &transform, frame, oldframe,
                                   backlerp, frontlerp)) {
    return true;
  }
#endif

  for (int mesh_index = 0; mesh_index < model->nummeshes; mesh_index++) {
    const maliasmesh_t *mesh = &model->meshes[mesh_index];
    if (!mesh->verts || !mesh->indices || mesh->numindices < 3 ||
        mesh->numverts <= 0) {
      continue;
    }
    const maliasvert_t *verts =
        GL_Shadow_ModelVertexData(model, mesh->verts);
    const uint16_t *indices =
        GL_Shadow_ModelIndexData(model, mesh->indices);
    for (int tri = 0; tri + 2 < mesh->numindices; tri += 3) {
      vec3_t world[3];
      bool valid = true;
      for (int corner = 0; corner < 3; corner++) {
        int vertex_index = indices[tri + corner];
        if (vertex_index < 0 || vertex_index >= mesh->numverts) {
          valid = false;
          break;
        }
        vec3_t local;
        GL_Shadow_AliasVertexLocal(model, verts, mesh->numverts, vertex_index,
                                   frame, oldframe, backlerp, frontlerp,
                                   local);
        GL_Shadow_TransformCasterPoint(&transform, local, world[corner]);
      }
      if (valid) {
        GL_Shadow_AddTriangle(world[0], world[1], world[2]);
      }
    }
  }
  return true;
}

static bool GL_Shadow_DrawBspCaster(const shadow_caster_t *caster,
                                    const shadow_view_desc_t *view) {
  const entity_t *ent = &caster->entity;
  const bsp_t *bsp = gl_static.world.cache;
  if (!bsp || !bsp->models || !bsp->faces || !bsp->vertices ||
      !bsp->edges || !(ent->model & BIT(31))) {
    return false;
  }

  int model_index = ~ent->model;
  if (model_index < 1 || model_index >= bsp->nummodels) {
    return false;
  }

  const mmodel_t *model = &bsp->models[model_index];
  if (!model->firstface || model->numfaces <= 0) {
    return true;
  }

  glShadowCasterTransform_t transform;
  GL_Shadow_BuildCasterTransform(ent, &transform);

  for (int i = 0; i < model->numfaces; i++) {
    const mface_t *face = &model->firstface[i];
    if (!face->firstsurfedge || face->numsurfedges < 3 ||
        (face->drawflags & (SURF_SKY | SURF_NODRAW | SURF_TRANS_MASK))) {
      continue;
    }

    const mvertex_t *v0 = GL_Shadow_SurfEdgeVertex(bsp, &face->firstsurfedge[0]);
    if (!v0) {
      continue;
    }
    const bool alpha_test = (face->drawflags & SURF_ALPHATEST) != 0;
    if (alpha_test && GL_Shadow_BeginAlphaFace(face, view)) {
      for (int j = 1; j < face->numsurfedges - 1; j++) {
        const mvertex_t *v1 = GL_Shadow_SurfEdgeVertex(bsp,
                                                       &face->firstsurfedge[j]);
        const mvertex_t *v2 = GL_Shadow_SurfEdgeVertex(bsp,
                                                       &face->firstsurfedge[j + 1]);
        if (!v1 || !v2) {
          continue;
        }
        vec3_t p0, p1, p2;
        GL_Shadow_TransformCasterPoint(&transform, v0->point, p0);
        GL_Shadow_TransformCasterPoint(&transform, v1->point, p1);
        GL_Shadow_TransformCasterPoint(&transform, v2->point, p2);
        vec3_t texture_points[3];
        VectorCopy(v0->point, texture_points[0]);
        VectorCopy(v1->point, texture_points[1]);
        VectorCopy(v2->point, texture_points[2]);
        GL_Shadow_AddAlphaTriangle(p0, p1, p2, face, texture_points);
      }
      GL_Shadow_EndAlphaFace(view);
      continue;
    }
    for (int j = 1; j < face->numsurfedges - 1; j++) {
      const mvertex_t *v1 = GL_Shadow_SurfEdgeVertex(bsp,
                                                     &face->firstsurfedge[j]);
      const mvertex_t *v2 = GL_Shadow_SurfEdgeVertex(bsp,
                                                     &face->firstsurfedge[j + 1]);
      if (!v1 || !v2) {
        continue;
      }
      vec3_t p0, p1, p2;
      GL_Shadow_TransformCasterPoint(&transform, v0->point, p0);
      GL_Shadow_TransformCasterPoint(&transform, v1->point, p1);
      GL_Shadow_TransformCasterPoint(&transform, v2->point, p2);
      GL_Shadow_AddTriangle(p0, p1, p2);
    }
  }
  return true;
}

static bool GL_Shadow_DrawBoundsCaster(const shadow_caster_t *caster) {
  if (!caster || !(caster->flags & RF_CASTSHADOW)) {
    return false;
  }

  const vec_t *mins = caster->bounds[0];
  const vec_t *maxs = caster->bounds[1];
  if (maxs[0] <= mins[0] || maxs[1] <= mins[1] || maxs[2] <= mins[2]) {
    return false;
  }

  vec3_t corners[8];
  for (int i = 0; i < 8; i++) {
    VectorSet(corners[i],
              (i & 1) ? maxs[0] : mins[0],
              (i & 2) ? maxs[1] : mins[1],
              (i & 4) ? maxs[2] : mins[2]);
  }

  static const int tris[12][3] = {
      {0, 2, 3}, {0, 3, 1},
      {4, 5, 7}, {4, 7, 6},
      {0, 1, 5}, {0, 5, 4},
      {2, 6, 7}, {2, 7, 3},
      {0, 4, 6}, {0, 6, 2},
      {1, 3, 7}, {1, 7, 5},
  };
  for (int i = 0; i < 12; i++) {
    GL_Shadow_AddTriangle(corners[tris[i][0]],
                          corners[tris[i][1]],
                          corners[tris[i][2]]);
  }
  return true;
}

static bool GL_Shadow_DrawCasterGeometry(const shadow_caster_t *caster,
                                         const shadow_view_desc_t *view) {
  if (!caster) {
    return false;
  }
  if (!caster->model) {
    return GL_Shadow_DrawBoundsCaster(caster);
  }
  bool submitted = false;
  if (caster->model & BIT(31)) {
    submitted = GL_Shadow_DrawBspCaster(caster, view);
  } else {
    submitted = GL_Shadow_DrawAliasCaster(caster);
  }
  return submitted || GL_Shadow_DrawBoundsCaster(caster);
}

static bool GL_Shadow_ViewTouchesBounds(const shadow_view_desc_t *view,
                                        const vec3_t mins,
                                        const vec3_t maxs) {
  vec3_t center, extents;
  VectorAvg(mins, maxs, center);
  VectorSubtract(maxs, mins, extents);
  float radius = VectorLength(extents) * 0.5f;

  if (view->view_type == SHADOW_VIEW_SUN_CASCADE) {
    float half = max(view->ortho_size * 0.5f, 64.0f);
    vec3_t delta;
    VectorSubtract(center, view->origin, delta);
    float side = fabsf(DotProduct(delta, view->axis[1]));
    float up = fabsf(DotProduct(delta, view->axis[2]));
    float forward = -DotProduct(delta, view->axis[0]);
    return side <= half + radius && up <= half + radius &&
           forward <= view->far_z + radius;
  }

  vec3_t delta;
  VectorSubtract(center, view->origin, delta);
  float dist = VectorLength(delta);
  if (dist - radius > view->far_z || dist + radius < view->near_z) {
    return false;
  }

  float forward = DotProduct(delta, view->axis[0]);
  if (forward + radius < view->near_z || forward - radius > view->far_z) {
    return false;
  }
  float side = fabsf(DotProduct(delta, view->axis[1]));
  float up = fabsf(DotProduct(delta, view->axis[2]));
  float tan_x = tanf(DEG2RAD(max(view->fov_x, 1.0f)) * 0.5f);
  float tan_y = tanf(DEG2RAD(max(view->fov_y, 1.0f)) * 0.5f);
  return side <= forward * tan_x + radius &&
         up <= forward * tan_y + radius;
}

static const glShadowFaceBounds_t *GL_Shadow_WorldFaceBounds(const bsp_t *bsp) {
  if (gl_shadow_world_cache.bounds && gl_shadow_world_cache.bsp == bsp &&
      gl_shadow_world_cache.checksum == bsp->checksum &&
      gl_shadow_world_cache.numfaces == bsp->numfaces) {
    return gl_shadow_world_cache.bounds;
  }

  GL_Shadow_FreeWorldCache();
  if (!bsp || bsp->numfaces <= 0) {
    return NULL;
  }

  glShadowFaceBounds_t *bounds =
      R_Malloc(sizeof(*bounds) * bsp->numfaces);
  for (int i = 0; i < bsp->numfaces; i++) {
    const mface_t *face = &bsp->faces[i];
    ClearBounds(bounds[i].mins, bounds[i].maxs);
    if (!face->firstsurfedge) {
      continue;
    }
    for (int j = 0; j < face->numsurfedges; j++) {
      const mvertex_t *v = GL_Shadow_SurfEdgeVertex(bsp, &face->firstsurfedge[j]);
      if (v) {
        GL_Shadow_AddPointToBounds(v->point, bounds[i].mins, bounds[i].maxs);
      }
    }
  }

  gl_shadow_world_cache.bsp = bsp;
  gl_shadow_world_cache.checksum = bsp->checksum;
  gl_shadow_world_cache.numfaces = bsp->numfaces;
  gl_shadow_world_cache.bounds = bounds;
  return bounds;
}

static void GL_Shadow_DrawWorldDepth(const shadow_view_desc_t *view) {
  const bsp_t *bsp = gl_static.world.cache;
  if (!bsp || !bsp->faces || !bsp->edges || !bsp->vertices) {
    return;
  }

  const glShadowFaceBounds_t *bounds = GL_Shadow_WorldFaceBounds(bsp);
  if (!bounds) {
    return;
  }

  for (int i = 0; i < bsp->numfaces; i++) {
    const mface_t *face = &bsp->faces[i];
    if (!face->firstsurfedge || face->numsurfedges < 3) {
      continue;
    }
    int flags = face->drawflags;
    if (flags & (SURF_SKY | SURF_NODRAW | SURF_TRANS_MASK)) {
      continue;
    }

    const mvertex_t *v0 = GL_Shadow_SurfEdgeVertex(bsp, &face->firstsurfedge[0]);
    if (!v0) {
      continue;
    }
    gl_shadow.world_faces_considered++;
    if (view && !GL_Shadow_ViewTouchesBounds(view, bounds[i].mins,
                                             bounds[i].maxs)) {
      continue;
    }
    gl_shadow.world_faces_submitted++;
    const bool alpha_test = (flags & SURF_ALPHATEST) != 0;
    if (alpha_test && GL_Shadow_BeginAlphaFace(face, view)) {
      for (int j = 1; j < face->numsurfedges - 1; j++) {
        const mvertex_t *v1 = GL_Shadow_SurfEdgeVertex(bsp,
                                                       &face->firstsurfedge[j]);
        const mvertex_t *v2 = GL_Shadow_SurfEdgeVertex(bsp,
                                                       &face->firstsurfedge[j + 1]);
        if (!v1 || !v2) {
          continue;
        }
        vec3_t texture_points[3];
        VectorCopy(v0->point, texture_points[0]);
        VectorCopy(v1->point, texture_points[1]);
        VectorCopy(v2->point, texture_points[2]);
        GL_Shadow_AddAlphaTriangle(v0->point, v1->point, v2->point, face,
                                   texture_points);
      }
      GL_Shadow_EndAlphaFace(view);
      continue;
    }
    for (int j = 1; j < face->numsurfedges - 1; j++) {
      const mvertex_t *v1 = GL_Shadow_SurfEdgeVertex(bsp,
                                                     &face->firstsurfedge[j]);
      const mvertex_t *v2 = GL_Shadow_SurfEdgeVertex(bsp,
                                                     &face->firstsurfedge[j + 1]);
      if (!v1 || !v2) {
        continue;
      }
      GL_Shadow_AddTriangle(v0->point, v1->point, v2->point);
    }
  }
}

static void GL_Shadow_SetupViewMatrices(const shadow_view_desc_t *view,
                                        mat4_t view_matrix,
                                        mat4_t proj_matrix) {
  Matrix_FromOriginAxis(view->origin, view->axis, view_matrix);

  if (view->view_type == SHADOW_VIEW_SUN_CASCADE) {
    float half = max(view->ortho_size * 0.5f, 64.0f);
    float near_z = max(view->near_z, 0.0f);
    float far_z = max(view->far_z, near_z + 1.0f);
    GL_Shadow_Ortho(-half, half, -half, half, -far_z, -near_z,
                    proj_matrix);
  } else {
    float near_z = max(view->near_z, 1.0f);
    float far_z = max(view->far_z, near_z + 1.0f);
    Matrix_Frustum(view->fov_x, view->fov_y, 1.0f, near_z, far_z,
                   proj_matrix);
  }
}

void GL_Shadow_Init(void) {
  memset(&gl_shadow, 0, sizeof(gl_shadow));
}

void GL_Shadow_Shutdown(void) {
  GL_Shadow_FreeWorldCache();
  GL_Shadow_DestroyResources();
  if (gl_shadow.program) {
    qglDeleteProgram(gl_shadow.program);
    gl_shadow.program = 0;
  }
  if (gl_shadow.moment_program) {
    qglDeleteProgram(gl_shadow.moment_program);
    gl_shadow.moment_program = 0;
  }
  if (gl_shadow.alpha_program) {
    qglDeleteProgram(gl_shadow.alpha_program);
    gl_shadow.alpha_program = 0;
  }
  if (gl_shadow.alpha_moment_program) {
    qglDeleteProgram(gl_shadow.alpha_moment_program);
    gl_shadow.alpha_moment_program = 0;
  }
  memset(&gl_shadow, 0, sizeof(gl_shadow));
}

void GL_Shadow_BeginFrame(void *userdata,
                          const shadow_frontend_policy_t *policy,
                          uint32_t required_page_count) {
  (void)userdata;
  (void)required_page_count;
  gl_shadow.frame_active = policy && policy->enabled;
  gl_shadow.policy = policy ? *policy : (shadow_frontend_policy_t){0};
  gl_shadow.max_active_page = -1;
  gl_shadow.sun_active = false;
  // Pages rendered before a mid-frame reallocation were destroyed with the
  // old texture while their resident entries stayed clean; keep reporting
  // "allocated" for one extra frame so every view re-renders once.
  gl_shadow.reallocated_last_frame = gl_shadow.reallocated_this_frame;
  gl_shadow.reallocated_this_frame = false;
  gl_shadow.moment_rendered = false;
  gl_shadow.world_faces_considered = 0;
  gl_shadow.world_faces_submitted = 0;

  memset(&gls.u_shadows, 0, sizeof(gls.u_shadows));
  gls.u_shadows.params[1] = GL_SHADOW_DEFAULT_STRENGTH;
  gls.u_shadows.sun[0] = -1.0f;

  for (int i = 0; i < MAX_DLIGHTS; i++) {
    for (int j = 0; j < SHADOW_FRONTEND_POINT_FACES; j++) {
      gl_shadow.lights[i].pages[j] = -1;
    }
    gl_shadow.lights[i].page_count = 0;
    gl_shadow.lights[i].view_type = SHADOW_VIEW_POINT_FACE;
  }
}

bool GL_Shadow_EnsurePage(void *userdata, const shadow_view_desc_t *view) {
  (void)userdata;
  if (!gl_shadow.frame_active || !view ||
      view->page.index >= GL_SHADOW_MAX_PAGES) {
    return false;
  }

  bool allocated = false;
  if (!GL_Shadow_EnsureResources(view->resolution, view->storage_family,
                                 &allocated)) {
    return false;
  }

  GL_Shadow_RegisterView(view);
  return allocated || gl_shadow.reallocated_this_frame ||
         gl_shadow.reallocated_last_frame;
}

bool GL_Shadow_RenderView(void *userdata, const shadow_view_desc_t *view,
                          const shadow_caster_t *casters,
                          const int *caster_indices, int caster_count) {
  (void)userdata;
  if (!gl_shadow.frame_active || !gl_shadow.resources_ok || !view ||
      view->page.index >= GL_SHADOW_MAX_PAGES) {
    return false;
  }

  mat4_t view_matrix;
  mat4_t proj_matrix;
  GL_Shadow_SetupViewMatrices(view, view_matrix, proj_matrix);

  qglBindFramebuffer(GL_FRAMEBUFFER, gl_shadow.fbo);
  GLenum draw_buffer = view->storage_family == SHADOW_STORAGE_MOMENT
      ? GL_COLOR_ATTACHMENT0
      : GL_NONE;
  qglDrawBuffers(1, &draw_buffer);
  if (qglReadBuffer) {
    qglReadBuffer(GL_NONE);
  }
  qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             gl_shadow.depth_array, 0, (GLint)view->page.index);
  if (view->storage_family == SHADOW_STORAGE_MOMENT) {
    qglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               gl_shadow.moment_array, 0,
                               (GLint)view->page.index);
  }
  qglViewport(0, 0, gl_shadow.resolution, gl_shadow.resolution);
  GLboolean scissor_enabled = qglIsEnabled(GL_SCISSOR_TEST);
  if (scissor_enabled) {
    qglDisable(GL_SCISSOR_TEST);
  }
  if (view->storage_family == SHADOW_STORAGE_MOMENT) {
    qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  } else {
    qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  }
  qglDepthMask(GL_TRUE);
  if (view->storage_family == SHADOW_STORAGE_MOMENT) {
    GLfloat clear_color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
    if (view->filter_family == SHADOW_FILTER_EVSM) {
      // Empty texels represent far-plane depth. EVSM compares warped depth,
      // so clearing them to unwarped (1, 1) falsely shadows untouched areas.
      const float far_moment = expf((float)GL_SHADOW_EVSM_EXPONENT);
      clear_color[0] = far_moment;
      clear_color[1] = far_moment * far_moment;
    }
    qglClearBufferfv(GL_COLOR, 0, clear_color);
  }
  qglClear(GL_DEPTH_BUFFER_BIT);
  GL_StateBits(GLS_CULL_DISABLE);
  qglEnable(GL_POLYGON_OFFSET_FILL);
  qglPolygonOffset(GL_Shadow_ViewSlopeBias(view),
                   GL_Shadow_ViewConstantBias(view));
  GL_Shadow_UseOpaqueProgram(view);

  GL_ForceMatrix(gl_identity, view_matrix);
  gl_backend->load_matrix(GL_PROJECTION, proj_matrix, gl_identity);
  GL_LoadUniforms();

  tess.numverts = 0;
  tess.numindices = 0;
  tess.flags = 0;
  tess.dlight_bits = 0;
  memset(tess.texnum, 0, sizeof(tess.texnum));

  GL_Shadow_DrawWorldDepth(view);
  for (int i = 0; i < caster_count; i++) {
    int caster_index = caster_indices ? caster_indices[i] : -1;
    if (caster_index < 0 || !casters) {
      continue;
    }
    const shadow_caster_t *caster = &casters[caster_index];
    // The frontend culls casters per light; cube faces of a point light
    // would otherwise re-skin and re-submit every caster six times.
    if (!GL_Shadow_ViewTouchesBounds(view, caster->bounds[0],
                                     caster->bounds[1])) {
      continue;
    }
    GL_Shadow_DrawCasterGeometry(caster, view);
  }
  GL_Shadow_FlushPositions();

  qglDisable(GL_POLYGON_OFFSET_FILL);
  qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (scissor_enabled) {
    qglEnable(GL_SCISSOR_TEST);
  }
  qglBindFramebuffer(GL_FRAMEBUFFER, glr.framebuffer_bound ? FBO_SCENE : 0);
  GL_Shadow_InvalidateState();
  if (view->storage_family == SHADOW_STORAGE_MOMENT) {
    gl_shadow.moment_rendered = true;
  }
  return true;
}

void GL_Shadow_EndFrame(void *userdata,
                        const shadow_frontend_stats_t *stats) {
  (void)userdata;
  (void)stats;

  if (gl_shadow.max_active_page >= 0 && gl_shadow.resources_ok) {
    gls.u_shadows.params[0] = (float)(gl_shadow.max_active_page + 1);
    gls.u_shadows.params[1] = GL_SHADOW_DEFAULT_STRENGTH;
    gls.u_shadows.params[2] = Q_clipf(gl_shadow.policy.softness, 0.25f, 4.0f);
    gls.u_shadows.params[3] = (float)gl_shadow.policy.filter_family;
  } else {
    gls.u_shadows.params[0] = 0.0f;
    gls.u_shadows.params[2] = 0.0f;
    gls.u_shadows.params[3] = 0.0f;
    gls.u_shadows.sun[0] = -1.0f;
  }

  if (gl_shadow.storage_family == SHADOW_STORAGE_MOMENT &&
      gl_shadow.moment_array && gl_shadow.moment_rendered) {
    GL_ForceTexture(TMU_SHADOW_MOMENT, gl_shadow.moment_array);
    qglGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  }

  gl_shadow.sun_active =
      gls.u_shadows.params[0] > 0.0f && gls.u_shadows.sun[0] >= 0.0f &&
      gls.u_shadows.sun[1] > 0.0f;

  if (gl_static.shadow_buffer) {
    GL_BindBuffer(GL_UNIFORM_BUFFER, gl_static.shadow_buffer);
    qglBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(gls.u_shadows),
                     &gls.u_shadows);
    c.uniformUploads++;
  }
}

const char *GL_Shadow_DescribeMaterialization(void *userdata) {
  (void)userdata;
  return gl_shadow.resources_ok
      ? va("%s 2d-array %s pages=%d size=%d mips=%d world=%d/%d",
           gl_shadow.storage_family == SHADOW_STORAGE_MOMENT
               ? "moment"
               : "depth-compare",
           gl_shadow.storage_family == SHADOW_STORAGE_MOMENT
               ? "RGBA16F"
               : "D24",
           gl_shadow.max_active_page + 1, gl_shadow.resolution,
           gl_shadow.storage_family == SHADOW_STORAGE_MOMENT
               ? gl_shadow.mip_levels
               : 1,
           gl_shadow.world_faces_submitted,
           gl_shadow.world_faces_considered)
      : "unallocated";
}

void GL_Shadow_FillDlight(int source_index, glDlight_t *out) {
  if (!out) {
    return;
  }

  Vector4Set(out->shadow_pages0, -1.0f, -1.0f, -1.0f, -1.0f);
  Vector4Set(out->shadow_pages1, -1.0f, -1.0f, 0.0f, 0.0f);
  if (source_index < 0 || source_index >= MAX_DLIGHTS) {
    return;
  }

  const glShadowLightPages_t *light = &gl_shadow.lights[source_index];
  if (light->page_count <= 0) {
    return;
  }

  for (int i = 0; i < 4; i++) {
    out->shadow_pages0[i] = (float)light->pages[i];
  }
  out->shadow_pages1[0] = (float)light->pages[4];
  out->shadow_pages1[1] = (float)light->pages[5];
  out->shadow_pages1[2] =
      light->view_type == SHADOW_VIEW_POINT_FACE ? 1.0f : 2.0f;
  out->shadow_pages1[3] = (float)light->page_count;
}

GLuint GL_Shadow_DepthTexture(void) {
  return gl_shadow.resources_ok ? gl_shadow.depth_array : 0;
}

GLuint GL_Shadow_MomentTexture(void) {
  return gl_shadow.resources_ok ? gl_shadow.moment_array : 0;
}

bool GL_Shadow_SunActive(void) {
  return gl_shadow.sun_active && gl_static.use_shaders;
}
