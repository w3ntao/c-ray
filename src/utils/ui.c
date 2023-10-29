//
//  ui.c
//  C-ray
//
//  Created by Valtteri Koskivuori on 19/02/2017.
//  Copyright © 2017-2021 Valtteri Koskivuori. All rights reserved.
//

#include "../includes.h"
#include "ui.h"

#include "../datatypes/image/imagefile.h"
#include "../renderer/renderer.h"
#include "logging.h"
#include "../datatypes/tile.h"
#include "../datatypes/image/texture.h"
#include "../datatypes/color.h"
#include "platform/thread.h"
#include "platform/signal.h"
#include "assert.h"
#include "logo.h"
#include "loaders/textureloader.h"
#include "args.h"

#include <dlfcn.h>

#include "../vendored/SDL2/SDL_render.h"
#include "../vendored/SDL2/SDL_events.h"

struct sdl_syms {
	void *lib;
	int (*SDL_VideoInit)(const char *driver_name);
	void (*SDL_VideoQuit)(void);
	void (*SDL_Quit)(void);
	const char *(*SDL_GetError)(void);
	void (*SDL_SetWindowIcon)(SDL_Window *window, SDL_Surface *icon);
	void (*SDL_FreeSurface)(SDL_Surface *surface);
	SDL_Surface *(*SDL_CreateRGBSurfaceFrom)(
		void *pixels,
		int width,
		int height,
		int depth,
		int pitch,
		uint32_t rmask,
		uint32_t gmask,
		uint32_t bmask,
		uint32_t amask);
	SDL_Window *(*SDL_CreateWindow)(
		const char *title,
		int x,
		int y,
		int w,
		int h,
		uint32_t flags);
	SDL_Renderer *(*SDL_CreateRenderer)(SDL_Window *window, int index, uint32_t flags);
	SDL_Texture *(*SDL_CreateTexture)(SDL_Renderer *renderer, uint32_t format, int access, int w, int h);
	void (*SDL_DestroyTexture)(SDL_Texture *texture);
	void (*SDL_DestroyRenderer)(SDL_Renderer *renderer);
	void (*SDL_DestroyWindow)(SDL_Window *window);
	void (*SDL_RenderPresent)(SDL_Renderer *renderer);
	int (*SDL_RenderSetLogicalSize)(SDL_Renderer *renderer, int w, int h);
	int (*SDL_SetRenderDrawBlendMode)(SDL_Renderer *renderer, SDL_BlendMode blend_mode);
	int (*SDL_SetTextureBlendMode)(SDL_Texture *texture, SDL_BlendMode blend_mode);
	int (*SDL_RenderSetScale)(SDL_Renderer *renderer, float scaleX, float scaleY);
	int (*SDL_PollEvent)(SDL_Event *event);
	int (*SDL_UpdateTexture)(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);
	int (*SDL_RenderCopy)(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect);

};

static void *try_find_sdl2_lib(void) {
	char *candidates[] = {
		"libSDL2-2.0.so",
		"libSDL2-2.0.0.dylib"
	};
	void *lib = NULL;
	for (size_t i = 0; i < (sizeof(candidates) / sizeof(*candidates)); ++i) {
		if ((lib = dlopen(candidates[i], RTLD_LAZY))) return lib;
	}

	logr(info, "Couldn't find SDL library, tried the following names: ");
	for (size_t i = 0; i < (sizeof(candidates) / sizeof(*candidates)); ++i) {
		logr(plain, "\"%s\" ", candidates[i]);
	}
	logr(plain, "\n");

	return NULL;
}

static struct sdl_syms *try_get_sdl2_syms(void) {
	void *sdl2 = try_find_sdl2_lib();
	if (!sdl2) return NULL;
	struct sdl_syms *syms = calloc(1, sizeof(*syms));
	*syms = (struct sdl_syms){
		.lib = sdl2,
		.SDL_VideoInit              = dlsym(sdl2, "SDL_VideoInit"),
		.SDL_VideoQuit              = dlsym(sdl2, "SDL_VideoQuit"),
		.SDL_Quit                   = dlsym(sdl2, "SDL_Quit"),
		.SDL_GetError               = dlsym(sdl2, "SDL_GetError"),
		.SDL_SetWindowIcon          = dlsym(sdl2, "SDL_SetWindowIcon"),
		.SDL_FreeSurface            = dlsym(sdl2, "SDL_FreeSurface"),
		.SDL_CreateRGBSurfaceFrom   = dlsym(sdl2, "SDL_CreateRGBSurfaceFrom"),
		.SDL_CreateWindow           = dlsym(sdl2, "SDL_CreateWindow"),
		.SDL_CreateRenderer         = dlsym(sdl2, "SDL_CreateRenderer"),
		.SDL_CreateTexture          = dlsym(sdl2, "SDL_CreateTexture"),
		.SDL_DestroyTexture         = dlsym(sdl2, "SDL_DestroyTexture"),
		.SDL_DestroyRenderer        = dlsym(sdl2, "SDL_DestroyRenderer"),
		.SDL_DestroyWindow          = dlsym(sdl2, "SDL_DestroyWindow"),
		.SDL_RenderPresent          = dlsym(sdl2, "SDL_RenderPresent"),
		.SDL_RenderSetLogicalSize   = dlsym(sdl2, "SDL_RenderSetLogicalSize"),
		.SDL_SetRenderDrawBlendMode = dlsym(sdl2, "SDL_SetRenderDrawBlendMode"),
		.SDL_SetTextureBlendMode    = dlsym(sdl2, "SDL_SetTextureBlendMode"),
		.SDL_RenderSetScale         = dlsym(sdl2, "SDL_RenderSetScale"),
		.SDL_PollEvent              = dlsym(sdl2, "SDL_PollEvent"),
		.SDL_UpdateTexture          = dlsym(sdl2, "SDL_UpdateTexture"),
		.SDL_RenderCopy             = dlsym(sdl2, "SDL_RenderCopy")
	};
	for (size_t i = 0; i < (sizeof(struct sdl_syms) / sizeof(void *)); ++i) {
		if (!((void **)syms)[i]) {
			logr(warning, "sdl_syms[%zu] is NULL\n", i);
			free(syms);
			return NULL;
		}
	}
	return syms;
}

struct sdl_window {
	struct sdl_syms *sym;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	SDL_Texture *overlayTexture;
	bool isBorderless;
	bool isFullScreen;
	float windowScale;
	
	unsigned width;
	unsigned height;
};


static void setWindowIcon(struct sdl_window *w) {
#ifndef NO_LOGO
	struct texture *icon = load_texture_from_buffer(logo_png_data, logo_png_data_len, NULL);
	uint32_t rmask;
	uint32_t gmask;
	uint32_t bmask;
	uint32_t amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	int shift = (icon->channels == 3) ? 8 : 0;
	rmask = 0xff000000 >> shift;
	gmask = 0x00ff0000 >> shift;
	bmask = 0x0000ff00 >> shift;
	amask = 0x000000ff >> shift;
#else
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = (icon->channels == 3) ? 0 : 0xff000000;
#endif
	SDL_Surface *iconSurface = w->sym->SDL_CreateRGBSurfaceFrom(icon->data.byte_p,
														(int)icon->width,
														(int)icon->height,
														(int)icon->channels * 8,
														(int)(icon->channels * icon->width),
														rmask, gmask, bmask, amask);
	w->sym->SDL_SetWindowIcon(w->window, iconSurface);
	w->sym->SDL_FreeSurface(iconSurface);
	destroyTexture(icon);
#endif
}

struct sdl_window *win_try_init(struct sdl_prefs *prefs, int width, int height) {
	if (!prefs->enabled) return NULL;
	struct sdl_window *w = calloc(1, sizeof(*w));
	w->sym = try_get_sdl2_syms();
	if (!w->sym) {
		free(w);
		return NULL;
	}

	w->isFullScreen = prefs->fullscreen;
	w->isBorderless = prefs->borderless;
	w->windowScale = prefs->scale;
	w->width = width;
	w->height = height;
	
	//Initialize SDL
	if (w->sym->SDL_VideoInit(NULL) < 0) {
		logr(warning, "SDL couldn't initialize, error: \"%s\"\n", w->sym->SDL_GetError());
		win_destroy(w);
		return NULL;
	}
	//Init window
	SDL_WindowFlags flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
	if (prefs->fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	if (prefs->borderless) flags |= SDL_WINDOW_BORDERLESS;
	flags |= SDL_WINDOW_RESIZABLE;

	w->window = w->sym->SDL_CreateWindow("c-ray © vkoskiv 2015-2023",
										 SDL_WINDOWPOS_UNDEFINED,
										 SDL_WINDOWPOS_UNDEFINED,
								 width * prefs->scale,
								 height * prefs->scale,
								 flags);
	if (w->window == NULL) {
		logr(warning, "Window couldn't be created, error: \"%s\"\n", w->sym->SDL_GetError());
		win_destroy(w);
		return NULL;
	}
	//Init renderer
	w->renderer = w->sym->SDL_CreateRenderer(w->window, -1, SDL_RENDERER_ACCELERATED);
	if (w->renderer == NULL) {
		logr(warning, "Renderer couldn't be created, error: \"%s\"\n", w->sym->SDL_GetError());
		win_destroy(w);
		return NULL;
	}
	
	w->sym->SDL_RenderSetLogicalSize(w->renderer, w->width, w->height);
	//And set blend modes
	w->sym->SDL_SetRenderDrawBlendMode(w->renderer, SDL_BLENDMODE_BLEND);
	
	w->sym->SDL_RenderSetScale(w->renderer, w->windowScale, w->windowScale);
	//Init pixel texture
	w->texture = w->sym->SDL_CreateTexture(w->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w->width, w->height);
	if (w->texture == NULL) {
		logr(warning, "Texture couldn't be created, error: \"%s\"\n", w->sym->SDL_GetError());
		win_destroy(w);
		return NULL;
	}
	//Init overlay texture (for UI info)

	uint32_t format = SDL_PIXELFORMAT_ABGR8888;
#if SDL_BYTEORDER == BIG_ENDIAN
	format = SDL_PIXELFORMAT_RGBA8888;
#endif
	w->overlayTexture = w->sym->SDL_CreateTexture(w->renderer, format, SDL_TEXTUREACCESS_STREAMING, w->width, w->height);
	if (w->overlayTexture == NULL) {
		logr(warning, "Overlay texture couldn't be created, error: \"%s\"\n", w->sym->SDL_GetError());
		win_destroy(w);
		return NULL;
	}
	
	//And set blend modes for textures too
	w->sym->SDL_SetTextureBlendMode(w->texture, SDL_BLENDMODE_BLEND);
	w->sym->SDL_SetTextureBlendMode(w->overlayTexture, SDL_BLENDMODE_BLEND);
	
	setWindowIcon(w);
	
	return w;
	// (void)prefs; (void)width; (void)height;
	// logr(warning, "Render preview is disabled. (No SDL2)\n");
	// return NULL;
}

void win_destroy(struct sdl_window *w) {
	if (!w) return;
	if (w->sym) {
		w->sym->SDL_VideoQuit();
		w->sym->SDL_Quit();
		if (w->texture) {
			w->sym->SDL_DestroyTexture(w->texture);
			w->texture = NULL;
		}
		if (w->overlayTexture) {
			w->sym->SDL_DestroyTexture(w->overlayTexture);
			w->texture = NULL;
		}
		if (w->renderer) {
			w->sym->SDL_DestroyRenderer(w->renderer);
			w->renderer = NULL;
		}
		if (w->window) {
			w->sym->SDL_DestroyWindow(w->window);
			w->window = NULL;
		}
		dlclose(w->sym->lib);
		free(w->sym);
	}
	free(w);
	w = NULL;
}

void printDuration(uint64_t ms) {
	logr(info, "Finished render in ");
	printSmartTime(ms);
	logr(plain, "                     \n");
}

void getKeyboardInput(struct renderer *r) {
	if (!r->sdl) return;
	SDL_Event event;
	while (r->sdl->sym->SDL_PollEvent(&event)) {
		if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
			if (event.key.keysym.sym == SDLK_s) {
				printf("\n");
				logr(info, "Aborting render, saving\n");
				r->state.renderAborted = true;
				r->state.saveImage = true;
			}
			if (event.key.keysym.sym == SDLK_x) {
				printf("\n");
				logr(info, "Aborting render without saving\n");
				r->state.renderAborted = true;
				r->state.saveImage = false;
			}
			if (event.key.keysym.sym == SDLK_p) {
				for (int i = 0; i < r->prefs.threadCount; ++i) {
					r->state.threadStates[i].paused = !r->state.threadStates[i].paused;
				}
			}
		}
		if (event.type == SDL_QUIT) {
			printf("\n");
			logr(info, "Aborting render without saving\n");
			r->state.renderAborted = true;
			r->state.saveImage = false;
		}
	}
}

static void clearProgBar(struct renderer *r, struct renderTile temp) {
	for (unsigned i = 0; i < temp.width; ++i) {
		setPixel(r->state.uiBuffer, g_clear_color, temp.begin.x + i, (temp.begin.y + (temp.height / 5)) - 1);
		setPixel(r->state.uiBuffer, g_clear_color, temp.begin.x + i, (temp.begin.y + (temp.height / 5))    );
		setPixel(r->state.uiBuffer, g_clear_color, temp.begin.x + i, (temp.begin.y + (temp.height / 5)) + 1);
	}
}

/*
 So this is a bit of a kludge, we get the dynamically updated completedSamples
 info that renderThreads report back, and then associate that with the static
 renderTiles array data that is only updated once a tile is completed.
 I didn't want to put any mutex locks in the main render loop, so this gets
 around that.
 */
static void drawProgressBars(struct renderer *r) {
	for (int t = 0; t < r->prefs.threadCount; ++t) {
		if (r->state.threadStates[t].currentTile) {
			struct renderTile *temp = r->state.threadStates[t].currentTile;
			int completedSamples = r->state.threadStates[t].completedSamples;
			int totalSamples = r->prefs.sampleCount;
			
			float prc = ((float)completedSamples / (float)totalSamples);
			int pixels2draw = (int)((float)temp->width * prc);

			struct color c = temp->state == rendering ? g_prog_color: g_clear_color;
			
			//And then draw the bar
			for (int i = 0; i < pixels2draw; ++i) {
				setPixel(r->state.uiBuffer, c, temp->begin.x + i, (temp->begin.y + (temp->height / 5)) - 1);
				setPixel(r->state.uiBuffer, c, temp->begin.x + i, (temp->begin.y + (temp->height / 5))    );
				setPixel(r->state.uiBuffer, c, temp->begin.x + i, (temp->begin.y + (temp->height / 5)) + 1);
			}
		}
	}
	for (int i = 0; i < r->state.tileCount; ++i) {
		if (r->state.renderTiles[i].state == finished) {
			clearProgBar(r, r->state.renderTiles[i]);
		}

	}
}

/**
 Draw highlight frame to show which tiles are rendering

 @param r Renderer
 @param tile Given renderTile
 */
static void drawFrame(struct texture *buf, struct renderTile tile, struct color c) {
	unsigned length = tile.width  <= 16 ? 4 : 8;
			 length = tile.height <= 16 ? 4 : 8;
	length = length > tile.width ? tile.width : length;
	length = length > tile.height ? tile.height : length;

	for (unsigned i = 1; i < length; ++i) {
		//top left
		setPixel(buf, c, tile.begin.x + i, tile.begin.y + 1);
		setPixel(buf, c, tile.begin.x + 1, tile.begin.y + i);
		
		//top right
		setPixel(buf, c, tile.end.x - i, tile.begin.y + 1);
		setPixel(buf, c, tile.end.x - 1, tile.begin.y + i);
		
		//Bottom left
		setPixel(buf, c, tile.begin.x + i, tile.end.y - 1);
		setPixel(buf, c, tile.begin.x + 1, tile.end.y - i);
		
		//bottom right
		setPixel(buf, c, tile.end.x - i, tile.end.y - 1);
		setPixel(buf, c, tile.end.x - 1, tile.end.y - i);
	}
}

static void updateFrames(struct renderer *r) {
	if (r->prefs.tileWidth < 8 || r->prefs.tileHeight < 8) return;
	for (int i = 0; i < r->state.tileCount; ++i) {
		struct renderTile tile = r->state.renderTiles[i];
		struct color c = tile.state == rendering ? g_frame_color : g_clear_color;
		drawFrame(r->state.uiBuffer, tile, c);
	}
}

void win_update(struct sdl_window *w, struct renderer *r, struct texture *t) {
	if (!w) return;
	//Render frames
	if (!isSet("interactive") || r->state.clients) {
		updateFrames(r);
		drawProgressBars(r);
	}
	//Update image data
	w->sym->SDL_UpdateTexture(w->texture, NULL, t->data.byte_p, (int)t->width * 3);
	w->sym->SDL_UpdateTexture(w->overlayTexture, NULL, r->state.uiBuffer->data.byte_p, (int)t->width * 4);
	w->sym->SDL_RenderCopy(w->renderer, w->texture, NULL, NULL);
	w->sym->SDL_RenderCopy(w->renderer, w->overlayTexture, NULL, NULL);
	w->sym->SDL_RenderPresent(w->renderer);
}
