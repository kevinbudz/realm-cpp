#pragma once
// The GL entry points of this build: GLEW (OpenGL 3.3 core, native) or the GLES 3.0 header that
// WebGL 2 implements (Emscripten). REALM_GLES is 1 on the web only; code that needs a desktop-only
// entry point, enum or extension tests it.
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#define REALM_GLES 1
#else
#include <GL/glew.h>
#endif
