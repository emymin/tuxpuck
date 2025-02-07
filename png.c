/* png.c - Copyright (C) 2001-2002 Jacob Kroon, see COPYING for details */
/* PNG code originally by Philippe Lavoie, lavoie@zeus.genie.uottawa.ca */

#include <stdlib.h>
#include <SDL_video.h>
#include <SDL_rwops.h>
#include <SDL_error.h>
#include <SDL_endian.h>
#ifdef macintosh
#define MACOS
#endif
#include <png.h>

/* png code */
static void png_read_data(png_structp ctx, png_bytep area, png_size_t size)
{
  SDL_RWops *src;

  src = (SDL_RWops *) png_get_io_ptr(ctx);
  SDL_RWread(src, area, size, 1);
}

SDL_Surface *loadPNG(Uint8 * data, Uint32 * memcounter)
{
  SDL_Surface *volatile surface;
  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type;
  Uint32 Rmask;
  Uint32 Gmask;
  Uint32 Bmask;
  Uint32 Amask;
  SDL_Palette *palette;
  png_bytep *volatile row_pointers;
  int row, i;
  volatile int ckey = -1;
  png_color_16 *transv;
  SDL_RWops *src = NULL;
  Uint32 size;

  memcpy(&size, data, sizeof(Uint32));
  if (memcounter)
    *memcounter += size + sizeof(Uint32);
  data += sizeof(Uint32);
  src = SDL_RWFromMem(data, size);

  /* Initialize the data we will clean up when we're done */
  png_ptr = NULL;
  info_ptr = NULL;
  row_pointers = NULL;
  surface = NULL;

  /* Check to make sure we have something to do */
  if (!src) {
    goto done;
  }

  /* Create the PNG loading context structure */
  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (png_ptr == NULL) {
    SDL_SetError("Couldn't allocate memory for PNG file");
    goto done;
  }

  /* Allocate/initialize the memory for image information.  REQUIRED. */
  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL) {
    SDL_SetError("Couldn't create image information for PNG file");
    goto done;
  }

  /* Set error handling if you are using setjmp/longjmp method (this is
   * the normal method of doing things with libpng).  REQUIRED unless you
   * set up your own error handlers in png_create_read_struct() earlier.
   */
  if (setjmp(png_jmpbuf(png_ptr))) {
    SDL_SetError("Error reading the PNG file.");
    goto done;
  }

  /* Set up the input control */
  png_set_read_fn(png_ptr, src, png_read_data);

  /* Read PNG header info */
  png_read_info(png_ptr, info_ptr);
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth,
	       &color_type, &interlace_type, NULL, NULL);

  /* tell libpng to strip 16 bit/color files down to 8 bits/color */
  png_set_strip_16(png_ptr);

  /* Extract multiple pixels with bit depths of 1, 2, and 4 from a single
   * byte into separate bytes (useful for paletted and grayscale images).
   */
  png_set_packing(png_ptr);

  /* scale greyscale values to the range 0..255 */
  if (color_type == PNG_COLOR_TYPE_GRAY)
    png_set_expand(png_ptr);

  /* For images with a single "transparent colour", set colour key;
     if more than one index has transparency, or if partially transparent
     entries exist, use full alpha channel */
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
    int num_trans;
    Uint8 *trans;
    png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &transv);
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
      /* Check if all tRNS entries are opaque except one */
      int i, t = -1;
      for (i = 0; i < num_trans; i++)
	if (trans[i] == 0) {
	  if (t >= 0)
	    break;
	  t = i;
	} else if (trans[i] != 255)
	  break;
      if (i == num_trans) {
	/* exactly one transparent index */
	ckey = t;
      } else {
	/* more than one transparent index, or translucency */
	png_set_expand(png_ptr);
      }
    } else
      ckey = 0;					 /* actual value will be set later */
  }

  if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png_ptr);

  png_read_update_info(png_ptr, info_ptr);

  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth,
	       &color_type, &interlace_type, NULL, NULL);

  /* Allocate the SDL surface to hold the image */
  Rmask = Gmask = Bmask = Amask = 0;
  if (color_type != PNG_COLOR_TYPE_PALETTE) {
    if (SDL_BYTEORDER == SDL_LIL_ENDIAN) {
      Rmask = 0x000000FF;
      Gmask = 0x0000FF00;
      Bmask = 0x00FF0000;
      Amask = (png_get_channels(png_ptr,info_ptr) == 4) ? 0xFF000000 : 0;
    } else {
      int s = (png_get_channels(png_ptr,info_ptr) == 4) ? 0 : 8;
      Rmask = 0xFF000000 >> s;
      Gmask = 0x00FF0000 >> s;
      Bmask = 0x0000FF00 >> s;
      Amask = 0x000000FF >> s;
    }
  }
  surface = SDL_AllocSurface(SDL_SWSURFACE, width, height,
			     bit_depth * png_get_channels(png_ptr,info_ptr), Rmask, Gmask,
			     Bmask, Amask);
  if (surface == NULL) {
    SDL_SetError("Out of memory");
    goto done;
  }

  if (ckey != -1) {
    if (color_type != PNG_COLOR_TYPE_PALETTE)
      /* FIXME: Should these be truncated or shifted down? */
      ckey = SDL_MapRGB(surface->format,
			(Uint8) transv->red,
			(Uint8) transv->green, (Uint8) transv->blue);
    SDL_SetColorKey(surface, SDL_SRCCOLORKEY, ckey);
  }

  /* Create the array of pointers to image data */
  row_pointers = (png_bytep *) malloc(sizeof(png_bytep) * height);
  if ((row_pointers == NULL)) {
    SDL_SetError("Out of memory");
    SDL_FreeSurface(surface);
    surface = NULL;
    goto done;
  }
  for (row = 0; row < (int) height; row++) {
    row_pointers[row] = (png_bytep)
      (Uint8 *) surface->pixels + row * surface->pitch;
  }

  /* Read the entire image in one go */
  png_read_image(png_ptr, row_pointers);

  /* read rest of file, get additional chunks in info_ptr - REQUIRED */
  png_read_end(png_ptr, info_ptr);

  /* Load the palette, if any */
  palette = surface->format->palette;

  int num_palette;
  png_colorp pal;
  png_get_PLTE(png_ptr,info_ptr,&pal,&num_palette);

  if (palette) {
    if (color_type == PNG_COLOR_TYPE_GRAY) {
      palette->ncolors = 256;
      for (i = 0; i < 256; i++) {
        palette->colors[i].r = i;
        palette->colors[i].g = i;
        palette->colors[i].b = i;
      }
    } else if (num_palette > 0) {
      palette->ncolors = num_palette;
      for (i = 0; i < num_palette; ++i) {
        palette->colors[i].b = pal[i].blue;
        palette->colors[i].g = pal[i].green;
        palette->colors[i].r = pal[i].red;
      }
    }
  }

done:						 /* Clean up and return */
  png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : (png_infopp) 0,
			  (png_infopp) 0);
  if (row_pointers) {
    free(row_pointers);
  }
  if (src)
    SDL_FreeRW(src);
  return (surface);
}
