/*
 *  pslash - a lightweight framebuffer splashscreen for embedded devices.
 *
 *  Copyright (c) 2006 Matthew Allum <mallum@o-hand.com>
 *
 *  Parts of this file ( fifo handling ) based on 'usplash' copyright
 *  Matthew Garret.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include "psplash.h"
#include "psplash-poky-img.h"
#include "psplash-bar-img.h"
#include "radeon-font.h"
#include "customizations.h"
#include <unistd.h>

#define PROGRESS_FILE "/tmp/splash_progress"

// Global variable indicating the font scale factor: 0=>1x 1=>2x 2=>4x
int FONT_SCALE;
bool disable_progress_bar = FALSE;
bool fastboot_enable = FALSE;

void
psplash_exit (int signum)
{
    DBG("mark");

    psplash_console_reset ();
}

void
psplash_draw_msg (PSplashFB *fb, const char *msg)
{
    int w, h;

    psplash_fb_text_size (fb, &w, &h, &radeon_font, msg);

    DBG("displaying '%s' %ix%i\n", msg, w, h);

    /* Clear */
    if(FALSE == fastboot_enable)
        psplash_fb_draw_rect (fb,
                              0,
                              0,
                              10,
                              fb->width,
                              h+10,
                              PSPLASH_TEXTBK_COLOR);
    else
        psplash_fb_draw_rect (fb,
                              0,
                              0,
                              10,
                              fb->width,
                              h+10,
                              PSPLASH_TEXTBK_COLOR_FB);

    psplash_fb_draw_text (fb,
                          0,
                          (fb->width-w)/2,
                          15,
                          PSPLASH_TEXT_COLOR,
                          &radeon_font,
                          msg);
}

/*
 * IMPORTANT: keep appearance aligned with xsplash's draw_infinite_progress()
 * bar_rel_sz: how many times smaller is bar compared to background box
 * splash_progress: returned progress offset
 */
void
psplash_draw_infinite_progress (PSplashFB *fb, int bar_rel_sz, int *progress)
{
    if (disable_progress_bar)
        return;

    if (fb == NULL || bar_rel_sz <= 0 || progress == 0)
        return;

    int x, y, width, height, barwidth, xoff;
    static int step = 2;

    x      = (fb->width - BAR_IMG_WIDTH)/2;
    y      = fb->height - (fb->height/6);
    width  = BAR_IMG_WIDTH;
    height = BAR_IMG_HEIGHT;

    barwidth = (width + (bar_rel_sz-1)) / bar_rel_sz;  // equivalent to (width / bar_rel_sz) but rounded up

    // wrap around
    if (*progress == INT_MIN || (x + *progress) > (x + width))
        *progress = -barwidth;
    xoff = x + *progress;

    // draw background
    psplash_fb_draw_rect (fb, 1, x, y, width, height, PSPLASH_BAR_BACKGROUND_COLOR);

    // draw bar
    int overflow = xoff + barwidth - (x + width);
    if (overflow < 0)
        overflow = 0;
    if (*progress <= 0)
      psplash_fb_draw_rect (fb, 1, xoff, y, barwidth + *progress, height, PSPLASH_BAR_COLOR);
    else
      psplash_fb_draw_rect (fb, 1, xoff, y, barwidth - overflow, height, PSPLASH_BAR_COLOR);

    // use double buffering to avoid flickering due to overlapping rectangles
    psplash_fb_flush_rect (fb, x, y, width, height);

    *progress += step;
}

/*
 * IMPORTANT: keep appearance aligned with xsplash's draw_infinite_progress()
 */
void
psplash_draw_progress (PSplashFB *fb, int value)
{
  if (disable_progress_bar)
    return;

  int x, y, width, height, barwidth;

  x      = (fb->width  - BAR_IMG_WIDTH)/2;
  y      = fb->height - (fb->height/6) + 4;
  width  = BAR_IMG_WIDTH;
  height = BAR_IMG_HEIGHT;

  if (value > 0)
    {
      barwidth = (CLAMP(value,0,100) * width) / 100;
      psplash_fb_draw_rect (fb, 0, x + barwidth, y,
    			width - barwidth, height,
			PSPLASH_BAR_BACKGROUND_COLOR);
      psplash_fb_draw_rect (fb, 0, x, y, barwidth,
			    height, PSPLASH_BAR_COLOR);
    }
  else
    {
      barwidth = (CLAMP(-value,0,100) * width) / 100;
      psplash_fb_draw_rect (fb, 0, x, y,
    			width - barwidth, height,
			PSPLASH_BAR_BACKGROUND_COLOR);
      psplash_fb_draw_rect (fb, 0, x + width - barwidth,
			    y, barwidth, height,
			    PSPLASH_BAR_COLOR);
    }

  DBG("value: %i, width: %i, barwidth :%i\n", value,
		width, barwidth);
}

static int
parse_command (PSplashFB *fb, char *string, int length, bool infinite_progress, int progress)
{
  char *command;

  DBG("got cmd %s", string);
	
  if (strcmp(string, "QUIT") == 0)
    {
    if (progress)
      {
        FILE* fp;
        if ((fp = fopen(PROGRESS_FILE, "w")) == NULL)
          {
            fprintf(stderr, "Failed open progress file\n");
            return 1;
          }
        fprintf(fp, "%d\n", progress);
        fclose(fp);
      }
      return 1;
    }

  command = strtok(string," ");

  if (!infinite_progress && !strcmp(command,"PROGRESS"))
    {
      psplash_draw_progress (fb, atoi(strtok(NULL,"\0")));
    }
  else if (!strcmp(command,"MSG"))
    {
      psplash_draw_msg (fb, strtok(NULL,"\0"));
    }

  return 0;
}

void
psplash_main (PSplashFB *fb, int pipe_fd, bool disable_touch, bool infinite_progress)
{
    int            err;
    ssize_t        length = 0;
    fd_set         descriptors;
    struct timeval tv;
    char          *end;
    char           command[2048];
    int            taptap=0;
    int            laststatus=0;
    int            touch_fd = -1;
    // Keep track of current progress so it can be passed to xsplash
    // currently supports infinite progress mode only
    int            progress = INT_MIN;
    FILE* filePointer;
    char buffer[255];

    tv.tv_sec = 0;
    tv.tv_usec = 40000;

    FD_ZERO(&descriptors);
    FD_SET(pipe_fd, &descriptors);

    end = command;

    filePointer = fopen("/proc/cmdline", "r");
    if( fgets(buffer, sizeof(buffer), filePointer) )
    {
        if( strstr(buffer, "fastboot=y") )
        {
            fprintf(stdout, "%s: fastboot=y\n", __func__);
            fastboot_enable = TRUE;
        } else {
            fprintf(stdout, "%s: fastboot=n\n", __func__);
            fastboot_enable = FALSE;
        }
    }
    fclose(filePointer);

    while (1)
    {
startloop:
        // Handles tap-tap touchscreen sequence
        if(touch_fd < 0 && disable_touch == FALSE)
            touch_fd = Touch_open();

        if(Touch_handler(touch_fd, &taptap, &laststatus))
        {
            TapTap_Progress(fb, taptap);
            if(taptap > TAPTAP_TH)
            {
                if( FALSE == fastboot_enable )
                    TapTap_Detected(touch_fd, fb, laststatus);
                else
                    FastBootTapTap_Detected(touch_fd, fb, laststatus);

                return;
            }
        }

        if (vt_requested())
            return;

        err = select(pipe_fd+1, &descriptors, NULL, NULL, &tv);

        if (err <= 0)
        {
            if((err==0))
            { // This is the select(9 timeout case, needed only to handle the tap-tap sequence, so repeat the loop.

                tv.tv_sec = 0;
                tv.tv_usec = 20000;  // 40 ms repaint interval = 25fps

                FD_ZERO(&descriptors);
                FD_SET(pipe_fd,&descriptors);

                if (infinite_progress)
                    psplash_draw_infinite_progress(fb, 4, &progress);

                goto startloop;
            }
            return;
        }

        length += read (pipe_fd, end, sizeof(command) - (end - command));

        if (length == 0)
        {
            /* Reopen to see if there's anything more for us */
            close(pipe_fd);
            pipe_fd = open(PSPLASH_FIFO,O_RDONLY|O_NONBLOCK);
            goto out;
        }

        if (command[length-1] == '\0')
        {
            if (parse_command(fb, command, strlen(command), infinite_progress, progress))
                return;
            length = 0;
        }
        else if (command[length-1] == '\n')
        {
            command[length-1] = '\0';
            if (parse_command(fb, command, strlen(command), infinite_progress, progress))
                return;
            length = 0;
        }

out:
        end = &command[length];

        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        FD_ZERO(&descriptors);
        FD_SET(pipe_fd,&descriptors);
    }

    return;
}

int
main (int argc, char** argv)
{
    char      *tmpdir;
    int        pipe_fd, i = 0, angle = 0, ret = 0;
    PSplashFB *fb;
    bool       disable_console_switch = FALSE;
    bool       disable_touch = FALSE;
    bool       infinite_progress = FALSE;
    bool       blackscreen = FALSE;

    signal(SIGHUP, psplash_exit);
    signal(SIGINT, psplash_exit);
    signal(SIGQUIT, psplash_exit);

    while (++i < argc)
    {
        if (!strcmp(argv[i],"-n") || !strcmp(argv[i],"--no-console-switch"))
        {
            disable_console_switch = TRUE;
            continue;
        }

        if (!strcmp(argv[i],"-np") || !strcmp(argv[i],"--no-progress-bar"))
        {
            disable_progress_bar = TRUE;
            continue;
        }

        if (!strcmp(argv[i],"-xp") ||  !strcmp(argv[i],"--infinite-progress"))
        {
            infinite_progress = TRUE;
            continue;
        }

        if (!strcmp(argv[i],"-a") || !strcmp(argv[i],"--angle"))
        {
            if (++i >= argc) goto fail;
            angle = atoi(argv[i]);
            continue;
        }

        if (!strcmp(argv[i],"--notouch"))
        {
            disable_touch = TRUE;
            continue;
        }

        if (!strcmp(argv[i],"--blackscreen"))
        {
            blackscreen = TRUE;
            continue;
        }

fail:
        fprintf(stderr,
                "Usage: %s [-n|--no-console-switch][-a|--angle <0|90|180|270>][--notouch][-np|--no-progress-bar][-xp|--infinite-progress]\n",
                argv[0]);
        exit(-1);
    }

    tmpdir = getenv("TMPDIR");

    if (!tmpdir)
        tmpdir = "/tmp";

    chdir(tmpdir);

    if (mkfifo(PSPLASH_FIFO, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP))
    {
        if (errno!=EEXIST)
        {
            perror("mkfifo");
            exit(-1);
        }
    }

    pipe_fd = open (PSPLASH_FIFO,O_RDONLY|O_NONBLOCK);

    if (pipe_fd==-1)
    {
        perror("pipe open");
        exit(-2);
    }

    if (!disable_console_switch)
        psplash_console_switch ();

    if ((fb = psplash_fb_new(angle)) == NULL) {
        ret = -1;
        goto fb_fail;
    }

    /* Set the font size, based on the display resolution and screen orientation */
    if(fb->width < USESMALLFONT_TH)
        FONT_SCALE = 0; // Small fonts (scale = 1x)
    else
        FONT_SCALE = 1; // large fonts (scale = 2x)

    if(!blackscreen)
    {
        /* Clear the background with #ecece1 */
        psplash_fb_draw_rect (fb, 0, 0, 0, fb->width, fb->height,
                              PSPLASH_BACKGROUND_COLOR);

        if(-1 == psplash_draw_custom_splashimage(fb))
        {

            /* Draw the Poky logo  */
            psplash_fb_draw_image (fb, 0,
                                   (fb->width  - POKY_IMG_WIDTH)/2,
                                   ((fb->height * 5) / 6 - POKY_IMG_HEIGHT)/2,
                                   POKY_IMG_WIDTH,
                                   POKY_IMG_HEIGHT,
                                   POKY_IMG_BYTES_PER_PIXEL,
                                   POKY_IMG_RLE_PIXEL_DATA);
        }

        if (!infinite_progress)
            psplash_draw_progress (fb, 0);
    }

    UpdateBrightness();

    psplash_main (fb, pipe_fd, disable_touch, infinite_progress);

    psplash_fb_destroy (fb);

    // Clear the bootcounter
    setbootcounter(0);

fb_fail:
    unlink(PSPLASH_FIFO);

    if (!disable_console_switch)
        psplash_console_reset ();

    usleep(1000000);
    UpdateColorMatrix();

    return ret;
}
