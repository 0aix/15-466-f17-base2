#pragma once

//"GL.hpp" is a convenience header to include a minimal set of "modern" OpenGL function prototypes.
// -- this is in contrast to, e.g., SDL_OpenGL which may include a bunch of OpenGL1.2 cruft.

#define GL_GLEXT_PROTOTYPES 1
#include "glcorearb.h"
