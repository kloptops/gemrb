/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2012 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

#ifndef SDL12VIDEODRIVER_H
#define SDL12VIDEODRIVER_H

#include "SDLVideo.h"

namespace GemRB {

class SDL12VideoDriver : public SDLVideoDriver {
private:
	SDL_Surface* disp;

public:
	SDL12VideoDriver(void);
	
	int Init(void);
	int CreateDriverDisplay(const Size&, int bpp, const char* title);
	Sprite2D* GetScreenshot( Region r );

	bool SetFullscreenMode(bool set);

	bool ToggleGrabInput();
	void ShowSoftKeyboard();
	void HideSoftKeyboard();
	bool TouchInputEnabled();
	void SetGamma(int brightness, int contrast);

	bool SupportsBAMSprites() { return true; }

	void DrawLine(short x1, short y1, short x2, short y2, const Color& color);
	void DrawRect(const Region& rgn, const Color& color, bool fill = true);
	void DrawPoint(const Point& p, const Color& color);

private:
	void SwapBuffers(VideoBuffers&);
	SDLVideoDriver::vid_buf_t* CurrentRenderBuffer();
	VideoBuffer* NewVideoBuffer(const Region& rgn, BufferFormat fmt);
	bool SetSurfaceAlpha(SDL_Surface* surface, unsigned short alpha);

	int ProcessEvent(const SDL_Event & event);

	void DrawPoints(const std::vector<SDL_Point>& points, const SDL_Color& color);
	void DrawLines(const std::vector<SDL_Point>& points, const SDL_Color& color);
	void DrawHLine(short x1, short y, short x2, const Color& color);
	void DrawVLine(short x, short y1, short y2, const Color& color);
};

class SDLSurfaceVideoBuffer : public VideoBuffer {
	SDL_Surface* buffer;

public:
	SDLSurfaceVideoBuffer(SDL_Surface* surf, const Point& p)
	: VideoBuffer(Region(p, ::GemRB::Size(surf->w, surf->h)))
	{
		assert(surf);
		buffer = surf;

		Clear();
	}

	~SDLSurfaceVideoBuffer() {
		SDL_FreeSurface(buffer);
	}

	void Clear() {
		Uint32 ck = 0;
#if SDL_VERSION_ATLEAST(1,3,0)
		if (SDL_GetColorKey(buffer, &ck) == 0) {
#else
		ck = buffer->format->colorkey;
		if (ck) {
#endif
			SDL_FillRect(buffer, NULL, ck);
		} else {
			SDL_FillRect(buffer, NULL, SDL_MapRGBA(buffer->format, 0, 0, 0, SDL_ALPHA_TRANSPARENT));
		}
	}

	SDL_Surface* Surface() {
		return buffer;
	}

	bool RenderOnDisplay(void* display) const {
		SDL_Surface* sdldisplay = static_cast<SDL_Surface*>(display);
		SDL_Rect dst = RectFromRegion(rect);
		SDL_BlitSurface( buffer, NULL, sdldisplay, &dst );
		return true;
	}

	void CopyPixels(const Region& bufDest, const void* pixelBuf, const int* pitch = NULL, ...) {
		SDL_Surface* sprite = NULL;

		// we can safely const_cast pixelBuf because the surface is destroyed before return and we dont alter it

		// FIXME: this shold support everything from Video::BufferFormat
		if (buffer->format->BitsPerPixel == 16) { // RGB565
			sprite = SDL_CreateRGBSurfaceFrom( const_cast<void*>(pixelBuf), bufDest.w, bufDest.h, 16, 2 * bufDest.w, 0x7C00, 0x03E0, 0x001F, 0 );
		} else { // RGBPAL8
			sprite = SDL_CreateRGBSurfaceFrom( const_cast<void*>(pixelBuf), bufDest.w, bufDest.h, 8, bufDest.w, 0, 0, 0, 0 );
			va_list args;
			va_start(args, pitch);
			ieByte* pal = va_arg(args, ieByte*);
			for (int i = 0; i < 256; i++) {
				// FIXME: this should have been converted to a Palette in MVEPlayer
				// currently this is useless for other uses
				sprite->format->palette->colors[i].r = ( *pal++ ) << 2;
				sprite->format->palette->colors[i].g = ( *pal++ ) << 2;
				sprite->format->palette->colors[i].b = ( *pal++ ) << 2;
#if SDL_VERSION_ATLEAST(1,3,0)
				sprite->format->palette->colors[i].a = 0;
#else
				sprite->format->palette->colors[i].unused = 0;
#endif
			}
			va_end(args);
		}

		SDL_Rect dst = RectFromRegion(bufDest);
		SDL_BlitSurface(sprite, NULL, buffer, &dst);
		SDL_FreeSurface(sprite);
	}
};

class SDLOverlayVideoBuffer : public VideoBuffer {
	SDL_Overlay* overlay;
	Point renderPos;
	mutable bool changed;

public:
	SDLOverlayVideoBuffer(const Point& p, SDL_Overlay* overlay)
	: VideoBuffer(Region(p, ::GemRB::Size(overlay->w, overlay->h)))
	{
		assert(overlay);
		this->overlay = overlay;
		changed = false;
	}

	~SDLOverlayVideoBuffer() {
		SDL_FreeYUVOverlay(overlay);
	}

	void Clear() {}

	bool RenderOnDisplay(void* /*display*/) const {
		if (changed) {
			SDL_Rect dest = RectFromRegion(rect);
			SDL_DisplayYUVOverlay(overlay, &dest);
			changed = false;
			
			// IMPORTANT: if we ever wanted to combine rendering of overlay buffers with other buffers
			// we would need to blit the result back to the display buffer
			// I'm commenting it out because we currently only use these overlays for video
			// and we need all the CPU we can get for that
			// additionally, the changed flag probably won't work at that point
			
			
			//SDL_Surface* sdldisplay = static_cast<SDL_Surface*>(display);
			//SDL_Surface* sdl_disp = SDL_GetVideoSurface();
			//SDL_LowerBlit(sdl_disp, &dest, sdldisplay, &dest);
		}
		return false;
	}

	void CopyPixels(const Region& bufDest, const void* pixelBuf, const int* pitch = NULL, ...) {
		va_list args;
		va_start(args, pitch);

		enum PLANES {Y, U, V};
		const ieByte* planes[3];
		unsigned int strides[3];

		planes[Y] = static_cast<const ieByte*>(pixelBuf);
		strides[Y] = *pitch;
		planes[U] = va_arg(args, ieByte*);
		strides[U] = *va_arg(args, int*);
		planes[V] = va_arg(args, ieByte*);
		strides[V] = *va_arg(args, int*);

		va_end(args);

		SDL_LockYUVOverlay(overlay);
		for (unsigned int plane = 0; plane < 3; plane++) {
			const unsigned char *data = planes[plane];
			unsigned int size = overlay->pitches[plane];
			if (strides[plane] < size) {
				size = strides[plane];
			}
			unsigned int srcoffset = 0, destoffset = 0;
			for (int i = 0; i < ((plane == 0) ? bufDest.h : (bufDest.h / 2)); i++) {
				memcpy(overlay->pixels[plane] + destoffset,
					   data + srcoffset, size);
				srcoffset += strides[plane];
				destoffset += overlay->pitches[plane];
			}
		}
		SDL_UnlockYUVOverlay(overlay);
		renderPos = bufDest.Origin();
		changed = true;
	}
};

}

#endif
