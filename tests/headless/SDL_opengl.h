// Offscreen test build only (tests/headless): the OpenGL 1.1 names the desktop layer uses, for Mesa's
// libGL (the real build gets them from SDL's own SDL_opengl.h). Newer entry points are looked up at
// run time by the desktop layer itself.
#pragma once
#include <cstddef>
typedef unsigned int GLenum; typedef unsigned char GLboolean; typedef unsigned int GLbitfield; typedef int GLint;
typedef int GLsizei; typedef unsigned int GLuint; typedef float GLfloat; typedef double GLdouble; typedef void GLvoid;
typedef float GLclampf; typedef double GLclampd; typedef char GLchar; typedef unsigned char GLubyte; typedef short GLshort;
#ifndef APIENTRY
#define APIENTRY
#endif
extern "C" {
void glAlphaFunc(GLenum, GLclampf); void glBindTexture(GLenum, GLuint); void glBlendFunc(GLenum, GLenum);
void glClear(GLbitfield); void glClearColor(GLclampf, GLclampf, GLclampf, GLclampf); void glClearDepth(GLclampd);
void glColorPointer(GLint, GLenum, GLsizei, const void*); void glDeleteTextures(GLsizei, const GLuint*);
void glDepthFunc(GLenum); void glDepthMask(GLboolean); void glDisable(GLenum); void glDisableClientState(GLenum);
void glDrawArrays(GLenum, GLint, GLsizei); void glEnable(GLenum); void glEnableClientState(GLenum);
void glFogf(GLenum, GLfloat); void glFogfv(GLenum, const GLfloat*); void glFogi(GLenum, GLint);
void glGenTextures(GLsizei, GLuint*); void glGetFloatv(GLenum, GLfloat*); void glGetIntegerv(GLenum, GLint*);
const GLubyte* glGetString(GLenum); void glHint(GLenum, GLenum); void glLoadIdentity(void); void glLoadMatrixf(const GLfloat*);
void glMatrixMode(GLenum); void glOrtho(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
void glScissor(GLint, GLint, GLsizei, GLsizei); void glShadeModel(GLenum); void glTexCoordPointer(GLint, GLenum, GLsizei, const void*);
void glTexEnvi(GLenum, GLenum, GLint); void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void glTexParameterf(GLenum, GLenum, GLfloat); void glTexParameteri(GLenum, GLenum, GLint); void glVertexPointer(GLint, GLenum, GLsizei, const void*);
void glNormalPointer(GLenum, GLsizei, const void*); void glViewport(GLint, GLint, GLsizei, GLsizei);
void glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*); void glPixelStorei(GLenum, GLint); void glFinish(void);
void glColor4f(GLfloat, GLfloat, GLfloat, GLfloat); void glNormal3f(GLfloat, GLfloat, GLfloat);
void glClientActiveTexture(GLenum); void glActiveTexture(GLenum); void glColorMask(GLboolean, GLboolean, GLboolean, GLboolean);
void glCullFace(GLenum); void glPolygonOffset(GLfloat, GLfloat); void glReadBuffer(GLenum); void glDrawBuffer(GLenum);
}
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_LESS 0x0201
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_FOG 0x0B60
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66
#define GL_ALPHA_TEST 0x0BC0
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_FOG_HINT 0x0C54
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_NICEST 0x1102
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_SMOOTH 0x1D01
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_MODULATE 0x2100
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV 0x2300
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_RGBA8 0x8058
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_DEPTH_COMPONENT 0x1902
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_UNSIGNED_INT 0x1405
#define GL_NONE 0
#define GL_VIEWPORT 0x0BA2
extern "C" GLboolean glIsEnabled(GLenum);
