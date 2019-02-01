/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include <SDL.h>

#include "sys/platform.h"
#include "framework/Licensee.h"

#include "renderer/tr_local.h"

idCVar in_nograb("in_nograb", "0", CVAR_SYSTEM | CVAR_NOCHEAT, "prevents input grabbing");
#ifndef __EMSCRIPTEN__
idCVar r_waylandcompat("r_waylandcompat", "0", CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ARCHIVE, "wayland compatible framebuffer");
#endif

static bool grabbed = false;

static SDL_Window *window = NULL;
static SDL_GLContext context = NULL;

static void SetSDLIcon() {
#ifdef __EMSCRIPTEN__
  // Not needed on Emscripten
  return;
#else
  Uint32 rmask, gmask, bmask, amask;

  // ok, the following is pretty stupid.. SDL_CreateRGBSurfaceFrom() pretends to use a void* for the data,
  // but it's really treated as endian-specific Uint32* ...
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
  rmask = 0xff000000;
  gmask = 0x00ff0000;
  bmask = 0x0000ff00;
  amask = 0x000000ff;
#else
  rmask = 0x000000ff;
  gmask = 0x0000ff00;
  bmask = 0x00ff0000;
  amask = 0xff000000;
#endif

#include "doom_icon.h" // contains the struct d3_icon

  SDL_Surface *icon = SDL_CreateRGBSurfaceFrom((void *) d3_icon.pixel_data, d3_icon.width, d3_icon.height,
                                               d3_icon.bytes_per_pixel * 8, d3_icon.bytes_per_pixel * d3_icon.width,
                                               rmask, gmask, bmask, amask);

  SDL_SetWindowIcon(window, icon);

  SDL_FreeSurface(icon);
#endif
}

/*
===================
GLimp_Init
===================
*/
bool GLimp_Init(glimpParms_t parms) {
  common->Printf("Initializing OpenGL subsystem\n");

  assert(SDL_WasInit(SDL_INIT_VIDEO));

  Uint32 flags = SDL_WINDOW_OPENGL;

  if (parms.fullScreen)
    flags |= SDL_WINDOW_FULLSCREEN;

  int colorbits = 24;
  int depthbits = 24;
  int stencilbits = 8;

  for (int i = 0; i < 16; i++) {
    // 0 - default
    // 1 - minus colorbits
    // 2 - minus depthbits
    // 3 - minus stencil
    if ((i % 4) == 0 && i) {
      // one pass, reduce
      switch (i / 4) {
        case 2 :
          if (colorbits == 24)
            colorbits = 16;
          break;
        case 1 :
          if (depthbits == 24)
            depthbits = 16;
          else if (depthbits == 16)
            depthbits = 8;
        case 3 :
          if (stencilbits == 24)
            stencilbits = 16;
          else if (stencilbits == 16)
            stencilbits = 8;
      }
    }

    int tcolorbits = colorbits;
    int tdepthbits = depthbits;
    int tstencilbits = stencilbits;

    if ((i % 4) == 3) {
      // reduce colorbits
      if (tcolorbits == 24)
        tcolorbits = 16;
    }

    if ((i % 4) == 2) {
      // reduce depthbits
      if (tdepthbits == 24)
        tdepthbits = 16;
      else if (tdepthbits == 16)
        tdepthbits = 8;
    }

    if ((i % 4) == 1) {
      // reduce stencilbits
      if (tstencilbits == 24)
        tstencilbits = 16;
      else if (tstencilbits == 16)
        tstencilbits = 8;
      else
        tstencilbits = 0;
    }

    int channelcolorbits = 4;
    if (tcolorbits == 24)
      channelcolorbits = 8;

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, channelcolorbits);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, channelcolorbits);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, channelcolorbits);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, tdepthbits);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, tstencilbits);

#ifndef __EMSCRIPTEN__
    // Wayland compatibilty mode disabled on Emscripten
		if (r_waylandcompat.GetBool())
			SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
		else
#endif
      SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, channelcolorbits);
    SDL_GL_SetAttribute(SDL_GL_STEREO, parms.stereo ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, parms.multiSamples ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, parms.multiSamples);

    window = SDL_CreateWindow(ENGINE_VERSION,
                  SDL_WINDOWPOS_UNDEFINED,
                  SDL_WINDOWPOS_UNDEFINED,
                  parms.width, parms.height, flags);

    if (!window) {
      common->DPrintf("Couldn't set GL mode %d/%d/%d: %s",
              channelcolorbits, tdepthbits, tstencilbits, SDL_GetError());
      continue;
    }

    // Initialize ES 2.0 context profile, and do not set any other context flags (it does not work otherwise)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    context = SDL_GL_CreateContext(window);

  #ifndef WEBGL
    if (SDL_GL_SetSwapInterval(r_swapInterval.GetInteger()) < 0)
      common->Warning("SDL_GL_SWAP_CONTROL not supported");
  #endif

    SDL_GetWindowSize(window, &glConfig.vidWidth, &glConfig.vidHeight);

    SetSDLIcon(); // for SDL2  this must be done after creating the window

    glConfig.isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN;

    common->Printf("Using %d bits per color channel (RGBA), %d bits depth, %d bits stencil\n",
                   channelcolorbits, tdepthbits, tstencilbits);

    glConfig.colorBits = tcolorbits;
    glConfig.depthBits = tdepthbits;
    glConfig.stencilBits = tstencilbits;

    glConfig.displayFrequency = 0;

    break;
  }

  if (!window) {
    common->Warning("No usable GL mode found: %s", SDL_GetError());
    return false;
  }

  return true;
}

/*
===================
GLimp_SetScreenParms
===================
*/
bool GLimp_SetScreenParms(glimpParms_t parms) {
  common->DPrintf("TODO: GLimp_ActivateContext\n");
  return true;
}

/*
===================
GLimp_Shutdown
===================
*/
void GLimp_Shutdown() {
  common->Printf("Shutting down OpenGL subsystem\n");

  if (context) {
    SDL_GL_DeleteContext(context);
    context = NULL;
  }

  if (window) {
    SDL_DestroyWindow(window);
    window = NULL;
  }
}

/*
===================
GLimp_SwapBuffers
===================
*/
void GLimp_SwapBuffers() {
#ifdef WEBGL
  // SwapBuffer is not supported on WebGL. Swaps occurs when the code yields to the browser
  return;
#else
  SDL_GL_SwapWindow(window);
#endif
}

/*
=================
GLimp_SetGamma
=================
*/
void GLimp_SetGamma(unsigned short red[256], unsigned short green[256], unsigned short blue[256]) {
  if (!window) {
    common->Warning("GLimp_SetGamma called without window");
    return;
  }

#ifdef WEBGL
  // Changing the Gamma is not supported on WebGL. This have to be done differently.
  return;
#else
  if (SDL_SetWindowGammaRamp(window, red, green, blue))
    common->Warning("Couldn't set gamma ramp: %s", SDL_GetError());
#endif
}

/*
===================
GLimp_ExtensionPointer
===================
*/
GLExtension_t GLimp_ExtensionPointer(const char *name) {
  assert(SDL_WasInit(SDL_INIT_VIDEO));

  return (GLExtension_t) SDL_GL_GetProcAddress(name);
}

void GLimp_GrabInput(int flags) {
  bool grab = flags & GRAB_ENABLE;

  if (grab && (flags & GRAB_REENABLE))
    grab = false;

  if (flags & GRAB_SETSTATE)
    grabbed = grab;

  if (in_nograb.GetBool())
    grab = false;

  if (!window) {
    common->Warning("GLimp_GrabInput called without window");
    return;
  }

  SDL_ShowCursor(flags & GRAB_HIDECURSOR ? SDL_DISABLE : SDL_ENABLE);
  SDL_SetRelativeMouseMode((grab && (flags & GRAB_HIDECURSOR)) ? SDL_TRUE : SDL_FALSE);
  SDL_SetWindowGrab(window, grab ? SDL_TRUE : SDL_FALSE);
}
