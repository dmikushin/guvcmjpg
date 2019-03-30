/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>                            #
#                             Add UYVY color support(Macbook iSight)            #
#           Flemming Frandsen <dren.dk@gmail.com>                               #
#                             Add VU meter OSD                                  #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

#include <SDL.h>
#include <assert.h>
#include <signal.h>

#include "gview.h"
#include "gviewrender.h"

extern int verbosity;

static int desktop_w = 0;
static int desktop_h = 0;

static int bpp = 0;

static SDL_Surface *pscreen = NULL;
static SDL_Overlay *poverlay = NULL;

static SDL_Rect drect;

static Uint32 SDL_VIDEO_Flags =
        SDL_ANYFORMAT | SDL_RESIZABLE;

static const SDL_VideoInfo *info;

/*
 * initialize sdl video
 * args:
 *   width - video width
 *   height - video height
 *   flags - window flags:
 *              0- none
 *              1- fullscreen
 *              2- maximized
 *
 * asserts:
 *   none
 *
 * returns: pointer to SDL_Overlay
 */
static SDL_Overlay * video_init(int width, int height, int flags)
{
	if(verbosity > 0)
		printf("RENDER: Initializing SDL1 render\n");

	int video_w = width;
	int video_h = height;
	
    if (pscreen == NULL) //init SDL
    {
        char driver[128];
        /*----------------------------- Test SDL capabilities ---------------------*/
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE) < 0)
        {
            fprintf(stderr, "RENDER: Couldn't initialize SDL: %s\n", SDL_GetError());
            return NULL;
        }
        
        /* the SDL_INIT_NOPARACHUTE flag will capture fatal signals so that SDL can */
        /* clean up after itself. It works for things like SIGSEGV, but apparently  */
        /* SIGINT is not fatal enough. */
        signal(SIGINT, SIG_DFL);

        /*use hardware acceleration as default*/
        if ( ! getenv("SDL_VIDEO_YUV_HWACCEL") ) putenv("SDL_VIDEO_YUV_HWACCEL=1");
        if ( ! getenv("SDL_VIDEO_YUV_DIRECT") ) putenv("SDL_VIDEO_YUV_DIRECT=1");


        if (SDL_VideoDriverName(driver, sizeof(driver)) && verbosity > 0)
        {
            printf("RENDER: Video driver: %s\n", driver);
        }

        info = SDL_GetVideoInfo();

        if (info->wm_available && verbosity > 0) printf("RENDER: A window manager is available\n");

        if (info->hw_available)
        {
            if (verbosity > 0)
                printf("RENDER: Hardware surfaces are available (%dK video memory)\n", info->video_mem);

            SDL_VIDEO_Flags |= SDL_HWSURFACE;
            SDL_VIDEO_Flags |= SDL_DOUBLEBUF;
        }
        else
        {
            SDL_VIDEO_Flags |= SDL_SWSURFACE;
        }

        if (info->blit_hw)
        {
            if (verbosity > 0) printf("RENDER: Copy blits between hardware surfaces are accelerated\n");

            SDL_VIDEO_Flags |= SDL_ASYNCBLIT;
        }

        desktop_w = info->current_w; //get desktop width
        desktop_h = info->current_h; //get desktop height

        if(!desktop_w) desktop_w = 800;
        if(!desktop_h) desktop_h = 600;
        
        switch(flags)
	    {
		    case 2: /*maximize*/
		       video_w = desktop_w;
		       video_h = desktop_h;
		       break;
		    case 1: /*fullscreen*/
		       SDL_VIDEO_Flags |= SDL_FULLSCREEN;
		       break;
		    case 0:
		    default:
		       break;
	    }

        if (verbosity > 0)
        {
            if (info->blit_hw_CC) printf("Colorkey blits between hardware surfaces are accelerated\n");
            if (info->blit_hw_A) printf("Alpha blits between hardware surfaces are accelerated\n");
            if (info->blit_sw) printf("Copy blits from software surfaces to hardware surfaces are accelerated\n");
            if (info->blit_sw_CC) printf("Colorkey blits from software surfaces to hardware surfaces are accelerated\n");
            if (info->blit_sw_A) printf("Alpha blits from software surfaces to hardware surfaces are accelerated\n");
            if (info->blit_fill) printf("Color fills on hardware surfaces are accelerated\n");
        }

        SDL_WM_SetCaption("Guvcmjpg Video", NULL);

        /* enable key repeat */
        SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,SDL_DEFAULT_REPEAT_INTERVAL);
    }

    /*------------------------------ SDL init video ---------------------*/
    if(verbosity > 0)
    {
        printf("RENDER: Desktop resolution = %ix%i\n", desktop_w, desktop_h);
        printf("RENDER: Checking video mode %ix%i@32bpp\n", video_w, video_h);
    }

    bpp = SDL_VideoModeOK(
        video_w,
        video_h,
        32,
        SDL_VIDEO_Flags);

    if(!bpp)
    {
        fprintf(stderr, "RENDER: resolution not available\n");
        /*resize video mode*/
        if ((video_w > desktop_w) || (video_h > desktop_h))
        {
            video_w = desktop_w; /*use desktop video resolution*/
            video_h = desktop_h;
        }
        else
        {
            video_w = 800;
            video_h = 600;
        }
        fprintf(stderr, "RENDER: resizing to %ix%i\n", video_w, video_h);

    }
    else
    {
        if ((bpp != 32) && verbosity > 0) printf("RENDER: recomended color depth = %i\n", bpp);
    }

	if(verbosity > 0)
		printf("RENDER: setting video mode %ix%i@%ibpp\n", width, height, bpp);

    pscreen = SDL_SetVideoMode(
        video_w,
        video_h,
        bpp,
        SDL_VIDEO_Flags);

    if(pscreen == NULL)
    {
        return (NULL);
    }
    if(verbosity > 0)
		printf("RENDER: creating an overlay\n");
    /*use requested resolution for overlay even if not available as video mode*/
    SDL_Overlay* overlay=NULL;
    overlay = SDL_CreateYUVOverlay(width, height,
#ifdef USE_PLANAR_YUV
		SDL_IYUV_OVERLAY, /*yuv420p*/
#else
        SDL_YUY2_OVERLAY, /*yuv422*/
#endif
		pscreen);

    SDL_ShowCursor(SDL_DISABLE);
    return (overlay);
}

/*
 * init sdl1 render
 * args:
 *    width - overlay width
 *    height - overlay height
 *    flags - window flags:
 *              0- none
 *              1- fullscreen
 *              2- maximized
 *
 * asserts:
 *
 * returns: error code (0 ok)
 */
 int init_render_sdl1(int width, int height, int flags)
 {
	poverlay = video_init(width, height, flags);

	if(poverlay == NULL)
	{
		fprintf(stderr, "RENDER: Couldn't create yuv overlay (try disabling hardware accelaration)\n");
		return -1;
	}

	assert(pscreen != NULL);

	drect.x = 0;
	drect.y = 0;
	drect.w = pscreen->w;
	drect.h = pscreen->h;

	return 0;
 }

/*
 * render a frame
 * args:
 *   frame - pointer to frame data (yuyv format)
 *   width - frame width
 *   height - frame height
 *
 * asserts:
 *   poverlay is not nul
 *   frame is not null
 *
 * returns: error code
 */
int render_sdl1_frame(uint8_t *frame, int width, int height)
{
	/*asserts*/
	assert(poverlay != NULL);
	assert(frame != NULL);

	uint8_t *p = (uint8_t *) poverlay->pixels[0];
#ifdef USE_PLANAR_YUV
	int size = width * height * 3/2; /* for IYUV */
#else
	int size = width * height * 2; /* 2 bytes per pixel for yuyv*/
#endif
	 SDL_LockYUVOverlay(poverlay);
     memcpy(p, frame, size);

     SDL_UnlockYUVOverlay(poverlay);
     SDL_DisplayYUVOverlay(poverlay, &drect);
}

/*
 * set sdl1 render caption
 * args:
 *   caption - string with render window caption
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void set_render_sdl1_caption(const char* caption)
{
	SDL_WM_SetCaption(caption, NULL);
}

/*
 * dispatch sdl1 render events
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void render_sdl1_dispatch_events()
{
	SDL_Event event;

	/* Poll for events */
	while( SDL_PollEvent(&event) )
	{
		if(event.type==SDL_VIDEORESIZE)
		{
			pscreen =
				SDL_SetVideoMode(
					event.resize.w,
					event.resize.h,
					bpp,
					SDL_VIDEO_Flags);

			drect.w = event.resize.w;
			drect.h = event.resize.h;
		}

		if(event.type==SDL_QUIT)
		{
			if(verbosity > 0)
				printf("RENDER: (event) quit\n");
			render_call_event_callback(EV_QUIT);
		}
	}
}
/*
 * clean sdl1 render data
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void render_sdl1_clean()
{
	if(poverlay)
		SDL_FreeYUVOverlay(poverlay);
	poverlay = NULL;

	SDL_Quit();

	pscreen = NULL;
}

