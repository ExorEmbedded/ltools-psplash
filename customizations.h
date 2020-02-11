/*
 *  Contains declarations of the helper functions for Exor dedicated customizations.
 *
 *  Copyright (C) 2014 Exor s.p.a.
 *  Written by: Giovanni Pavoni Exor s.p.a.
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

#ifndef _HAVE_CUSTOMIZATIONS_H
#define _HAVE_CUSTOMIZATIONS_H

#include "psplash-fb.h"

#define TAPTAP_THLO 1
#define TAPTAP_TH   3
#define USESMALLFONT_TH 500

void Read_HWCode();
int psplash_draw_custom_splashimage(PSplashFB *fb);
void UpdateBrightness();
int Touch_handler(int touch_fd, int* taptap, int* laststatus);
void Touch_close(int touch_fd);
int Touch_open();
void TapTap_Progress(PSplashFB *fb, int taptap);
int TapTap_Detected(int touch_fd, PSplashFB *fb, int laststatus);
int FastBootTapTap_Detected(int touch_fd, PSplashFB *fb, int laststatus);
int setbootcounter(unsigned char val);
int UpdateColorMatrix();

#endif
