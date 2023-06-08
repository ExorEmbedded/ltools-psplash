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

#ifndef _HAVE_COMMON_H
#define _HAVE_COMMON_H

#define MAXPATHLENGTH           512
#define CMDLINEPATH             "/proc/"

#define ETOP507_VAL     104
#define ETOP507G_VAL    105
#define ECO_VAL         110
#define PLCM07_VAL      111
#define BE15A_VAL       114
#define EX7XX_VAL       115
#define ETOP6XXL_VAL    116
#define PGDXCA16_VAL    117
#define EX7XXQ_VAL      118
#define US03KITQ_VAL    119
#define ETOP705_VAL     120
#define WU16_VAL        121
#define US03WU16_VAL    122
#define AUTEC_VAL       123
#define JSMART_VAL      124
#define JSMARTQ_VAL     125
#define JSMARTTTL_VAL   126
#define NS01KIT_VAL     129
#define NS01KITOH_VAL   130
#define PGDXCA18_VAL    131
#define X5HH_VAL        136
#define X5BS_VAL        137
#define JS8XX_VAL       139
#define AB19_VAL        140
#define TA19_VAL        141
#define BE15B_VAL       142
#define NS02EKXX_VAL    143
#define NS02WU20_VAL    144
#define EX8XX_VAL       145
#define US04WU10_VAL    147
#define PGDXCA7LE_VAL   149
#define X5HHWIRED_VAL   150

#define IS_US01(X)      X == ECO_VAL || X == PLCM07_VAL || X == PGDXCA16_VAL || X == PGDXCA18_VAL || \
                        X == ETOP705_VAL || X == WU16_VAL || X == AUTEC_VAL || X == AB19_VAL || X == PGDXCA7LE_VAL

#define IS_US02(X)      X == ETOP6XXL_VAL

#define IS_US03(X)      X == BE15A_VAL || X == EX7XX_VAL || X == EX7XXQ_VAL || X == US03WU16_VAL || \
                        X == JSMART_VAL || X == JSMARTQ_VAL || X == JSMARTTTL_VAL || X == BE15B_VAL

#define IS_US04(X)      X == JS8XX_VAL || X == EX8XX_VAL

#define IS_NS01(X)      X == X5HH_VAL || X == X5BS_VAL || X == X5HHWIRED_VAL

#define IS_NS02(X)      X == NS02EKXX_VAL || X == NS02WU20_VAL

#include <stdio.h>

int atoi_s(char *s, int *val);

int gethwcode();
int gettouchtype();
int setbootcounter(unsigned char val);

int sysfs_read(char* pathto, char* fname, char* value, int n);
int sysfs_write(char* pathto, char* fname, char* value);

#endif
