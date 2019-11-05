/*
 *  Contains the helper functions for Exor dedicated customizations.
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


#include <stdio.h>

#include "common.h"
#include "psplash.h"
#include "psplash-fb.h"
#include "customizations.h"
#include <linux/i2c-dev.h>
#include <dirent.h> 
#include <linux/input.h>
#include "settings-img.h"
#include "configos-img.h"
#include "calib-img.h"
#include <math.h>

#define SPLASH_HDRLEN         56
#define SPLASH_STRIDE_IDX     0
#define SPLASH_SIZE_IDX       2

#define DEFAULT_SPLASHPARTITION          "/dev/mmcblk1p1"
#define PATHTOSPLASH                     "/mnt/factory"
#define SPLASHFILENAME                   "/splashimage.bin"

#define BRIGHTNESSDEVICE                 "/sys/class/backlight/"

#define SEEPROM_I2C_ADDRESS              "0-0054"
#define SEEPROM_I2C_BUS                  "i2c-0"
#define I2CSEEPROMDEVICE                 "/sys/class/i2c-dev/"SEEPROM_I2C_BUS"/device/"SEEPROM_I2C_ADDRESS"/eeprom"
#define BLDIMM_POS                       128

#define DEFAULT_TOUCH_EVENT0             "/dev/input/event0"
#define DEFAULT_TOUCH_EVENT1             "/dev/input/event1"
#define DEFAULT_TOUCH_EVENT2             "/dev/input/event2"

#define SYSPARAMS_CMD                    "/usr/bin/sys_params "

/***********************************************************************************************************
 STATIC HELPER FUNCTIONS
 ***********************************************************************************************************/

// Helper function to read a system parameter system.ini file
static int getSystemParameter(const char* key, char* value)
{

  char cmd[256];
  strcpy( cmd, SYSPARAMS_CMD);
  strncat(cmd, key, sizeof(cmd) - sizeof(SYSPARAMS_CMD) -1);

  FILE* pipe = popen(cmd, "r");
  if (!pipe)
    return -1;

  if ( fgets(value, sizeof(value), pipe) == NULL) {
    pclose(pipe);
    return -1;
  }

  return 0;
}

static int getSystemParameterInt(const char* key, int* value)
{

  char strValue[128];
  char* end;
  if ( getSystemParameter(key, strValue) < 0 )
      return -1;

  *value = (int) strtol(strValue, &end, 10);

  if (end == strValue)
     return -1;

  return 0;
}

//Helper function to show the specified icon
static void Draw_Icon(PSplashFB *fb, int iconw, int iconh, uint8* data, uint8 bkred, uint8 bkgreen, uint8 bkblue)
{
  #define ICONYPOS 100
  psplash_fb_draw_rect (fb, 0, (fb->width - iconw)/2, ICONYPOS, iconw, iconh, bkred, bkgreen, bkblue);
  
  psplash_fb_draw_image (fb, 
			 0,
			 (fb->width - iconw)/2, 
			 ICONYPOS,
			 iconw,
			 iconh,
			 SETTINGS_IMG_BYTES_PER_PIXEL,
			 data);
}


//Helper function to write the synchornization file with the JMloader
static int SyncJMLauncher(char* msg)
{
  char* tmpdir;
  char fullpath[MAXPATHLENGTH];
  
  //Filepath is defined by the TMPDIR env. variable; the default value is /tmp
  tmpdir = getenv("TMPDIR");
  if (!tmpdir)
    tmpdir = "/tmp";
  
  //Filename is "taptap"
  strncpy(fullpath, tmpdir, MAXPATHLENGTH);
  strncat(fullpath,"/taptap", MAXPATHLENGTH);
  
  // Opens the file and write the sync message
  FILE* fp;
  if((fp = fopen(fullpath, "w"))==NULL)
  {
    fprintf(stderr,"SyncJMLauncher cannot open file -> %s \n",fullpath);
    return -1;
  }
  
  fprintf(fp,"%s",msg);
  fflush(fp);
  fclose(fp);
  
  return 0;
}


//Helper function to execute a shell command (specified as string) and return 0 if command succeeds, -1 if failed
static int systemcmd(const char* cmd)
{
  int ret;
  
  ret = system(cmd);
  if (ret == -1)
    return ret;	// Failed to execute the system function
    
    if(WIFEXITED(ret))
    {
      if(0 != WEXITSTATUS(ret))
	return -1;	// The executed command exited normally but returned an error
	else
	  return 0;	// The executed command exited normally and returned 0 (=SUCCESS)
    }
    return -1;		// The executed command did not terminate properly 
}


// Helper function to read the brightness value from SEEPROM.
// A value in the range 0-255 is returned 
// NOTE: 0 means min. brightness, not backlight off.
// In case of error, 255 is returned, to be on the safe side.
static int get_brightness_from_seeprom()
{
  //1: Here we open the SEEPROM, which is supposed to be connected on the i2c-0 bus.
  FILE* fp;
  if((fp = fopen(I2CSEEPROMDEVICE, "rb"))==NULL)
  {
    fprintf(stderr, "psplash: Error eeprom_open: dev= %s err=%s\n", I2CSEEPROMDEVICE, strerror(errno));
    return 255;
  }
  
  //2: Now read the brightness value from the corresponding offset
  char buf = 255;
  
  fseek (fp, BLDIMM_POS, SEEK_SET);
  if(1 !=fread(&buf, 1, 1, fp))
    fprintf(stderr, "psplash: Error reading the eeprom: err=%s\n", strerror(errno));

  //3: Close and return value
  fclose(fp);
  return (((int)buf) & 0xff) ;
}


//Sets the brightness value as desired (without saving to i2c SEEPROM)
static int SetBrightness(char* brightnessdevice, int* pval)
{
  char strval[50];
  
  sprintf (strval,"%d", *pval);
  sysfs_write(brightnessdevice,"brightness",strval);
  return 0;
}

/***********************************************************************************************************
 Drawing the custom splashimage from splashimage.bin file

 NOTE: In order to speed up loading time, it is nedeed that both framebuffer and splashimage are in RGB565
       format.
***********************************************************************************************************/
int psplash_draw_custom_splashimage(PSplashFB *fb)
{
  char * splashpartition; //Partition containing the splashimage.bin file
  
  // Get the splash partition from the environment or use the default partition 
  splashpartition = getenv("SPLASHPARTITION");
  if (splashpartition == NULL)
    splashpartition = DEFAULT_SPLASHPARTITION;
  
  // Mount the splash partition 
  //char umount_cmd[] = "umount "PATHTOSPLASH; 
  char mkdir_cmd[] = "mkdir "PATHTOSPLASH; 
  systemcmd(mkdir_cmd);
  
  char mount_cmd[MAXPATHLENGTH] = "mount -o ro ";
  strncat(mount_cmd, splashpartition, MAXPATHLENGTH/2);
  mount_cmd[MAXPATHLENGTH/2] = 0;
  strcat(mount_cmd, " ");
  strcat(mount_cmd, PATHTOSPLASH);
  systemcmd(mount_cmd);
  
  //Try to open the splash file
  char splashfile[] = PATHTOSPLASH SPLASHFILENAME;
  
  FILE* fp;
  if((fp = fopen(splashfile, "rb"))==NULL)
  {
    fprintf(stderr,"psplash: cannot open splashimage file -> %s \n",splashfile);
    goto error;
  }
  
  // Gets the header of the SPLASH image and calculates the dimensions of the stored image. performs sanity checks
  unsigned int  splash_width;
  unsigned int  splash_height;
  unsigned int  splash_posx = 0;
  unsigned int  splash_posy = 0;
  unsigned int  header[SPLASH_HDRLEN];
  
  rewind(fp);
  
  if(fread(header, 1, SPLASH_HDRLEN, fp) != SPLASH_HDRLEN)
  {
    fprintf(stderr,"psplash: wrong splashimage file \n");
    goto error;
  }
  
  splash_width = (header[SPLASH_STRIDE_IDX]) / 2 + 1;

  if ((splash_width > fb->width) || (splash_width < 10))
  {
    fprintf(stderr,"psplash: splashimage width error: %d \n",splash_width);
    goto error;
  }
  
  splash_height = (((header[SPLASH_SIZE_IDX]) / 2) / splash_width);
  
  if (splash_height > (fb->height)) splash_height = (fb->height);
  if (splash_height < 10) 
  {
    fprintf(stderr,"psplash: splashimage height error: %d \n",splash_height);
    goto error;
  }
  
  // calculates the position of the splash inside the display
  splash_posx = ((fb->width)  - splash_width ) / 2;
  splash_posy = ((fb->height) - splash_height) / 2;
  
  //And now draws the splashimage
  int          x;
  int          y;
  uint8        red;
  uint8        green;
  uint8        blue;
  uint16*      stride;
  uint16       rgb565color;
  
  stride = (uint16 *) malloc (2 * (splash_width + 1));
  if (stride==NULL)
  {
    fprintf(stderr,"psplash: malloc error\n");
    goto error;
  }
  
  for(y=0; y<splash_height; y++)
  {
    fread(stride, 2 * splash_width, 1, fp);
    for(x=0; x<splash_width; x++)
    {
      rgb565color = stride[x];
      blue  = (uint8)((rgb565color << 3) & 0x00ff);
      green = (uint8)((rgb565color >> 3) & 0x00ff);
      red   = (uint8)((rgb565color >>8) & 0x00ff);
      psplash_fb_plot_pixel (fb, 0, splash_posx + x, splash_posy + y, red, green, blue);
    }
  }
  
  free(stride);
  fclose(fp);
  // UnMount the splash partition 
  // systemcmd(umount_cmd);
  
  return 0;

error:  
  if(fp)
    fclose(fp);
  // UnMount the splash partition 
  // systemcmd(umount_cmd);
  
  return -1;
}

/*! Apply gamma correction to dim light below physical backlight minimum value */

#define MXCFB_SET_GAMMA	       _IOW('F', 0x28, struct mxcfb_gamma)
#define MAX_GAMMA_LEVEL        30

struct mxcfb_gamma {
	int enable;
	int constk[16];
	int slopek[16];
};

/*! level between */
int applyGammaDimming(int level)
{
	int fd_fb = 0;
	struct mxcfb_gamma fb_gamma;
	int i, ret=0;
	int gamma;
	int constk[16], slopek[16];

  if(level > MAX_GAMMA_LEVEL || level < 0 )
    level = MAX_GAMMA_LEVEL;

  // Safety trim to allow minimum level
  if (level < 3)
    level = 3;

  if ((fd_fb = open("/dev/fb0", O_RDWR, 0)) < 0) {
    fprintf(stderr, "Unable to open /dev/fb0\n");
    ret = -1;
    goto done;
  }

  gamma = 10;
  constk[0] = 0;
  slopek[0] = gamma;
  for(i=1; i< 16; i++)
    {
    if(i>9)
      gamma += (gamma >> 2);

    constk[i] = constk[i-1] + gamma ;
    slopek[i] = gamma;
  }

  for(i=0; i< 16; i++)
  {
    constk[i] = (constk[i] * level) / MAX_GAMMA_LEVEL;
    slopek[i] = (slopek[i] * level) / MAX_GAMMA_LEVEL;
    fb_gamma.constk[i] = constk[i];
    fb_gamma.slopek[i] = slopek[i];
  }

  if(level != MAX_GAMMA_LEVEL)
    fb_gamma.enable = 1;
  else
    fb_gamma.enable = 0;

  if ( ioctl(fd_fb, MXCFB_SET_GAMMA, &fb_gamma) < 0) {
    fprintf(stderr, "Wrong gamma setting!\n");
    ret = -1;
    goto done;
  }

done:
  if (fd_fb)
    close(fd_fb);
  return ret;
}

/*
 * Structure used to define a 5*3 matrix of parameters for
 * setting IPU DP CSC module related to this framebuffer.
 */
struct mxcfb_csc_matrix {
	int param[5][3];
};

#define MXCFB_CSC_UPDATE_LCD	_IOW('F', 0x3F, struct mxcfb_csc_matrix)

int UpdateColorMatrix()
{
  int hue = 0;
  int white = 0;
  int sat_r = 100;
  int sat_g = 100;
  int sat_b = 100;
  int value;
  bool applyMatrix = FALSE;

  if ( getSystemParameterInt("screen/hue", &value) == 0 ) {
    hue = value;
    if ( hue < -100 )
      hue = -100;
    if ( hue > 100 )
      hue = 100;

    if ( hue != 0 )
      applyMatrix = TRUE;
  }

  if ( getSystemParameterInt("screen/whitebalance", &value) == 0 ) {
    white = value;
    if ( white < -100 )
      white = -100;
    if ( white > 100 )
      white = 100;

    if ( white != 0 )
      applyMatrix = TRUE;
  }

  if ( getSystemParameterInt("screen/saturation/red", &value) == 0 ) {
    sat_r = value;
    if ( sat_r < 0 )
      sat_r = 0;

    if ( ! (sat_r < 100) )
      sat_r = 100;
    else
      applyMatrix = TRUE;
  }

  if ( getSystemParameterInt("screen/saturation/green", &value) == 0 ) {
    sat_g = value;
    if ( sat_g < 0 )
      sat_g = 0;

    if ( ! (sat_g < 100) )
      sat_g = 100;
    else
      applyMatrix = TRUE;
  }

  if ( getSystemParameterInt("screen/saturation/blue", &value) == 0 ) {
    sat_b = value;
    if ( sat_b < 0 )
      sat_b = 0;

    if ( ! (sat_b < 100) )
      sat_b = 100;
    else
      applyMatrix = TRUE;
  }

  if (!applyMatrix)
    return 0;

  fprintf(stderr,"Applying hue %d, white %d, red %d, green %d, blue %d \n", hue, white, sat_r, sat_g, sat_b);

  static double color_correction_matrix[5][3] = {
    {  1.0,  0.0,  0.0 },
    {  0.0,  1.0,  0.0 },
    {  0.0,  0.0,  1.0 },
  };

  double hue_coeff;
  if(hue < 0)
    hue_coeff = 360 + (30 * ((float)hue/100.));
  else
    hue_coeff = 30 * ((float)hue/100.);

  double white_coeff = ((float)(white)/800.);
  double sat_r_coeff = ((float)(sat_r)/100.);
  double sat_g_coeff = ((float)(sat_g)/100.);
  double sat_b_coeff = ((float)(sat_b)/100.);

  int i,j, k;
  const double cosA = cos(hue_coeff*3.14159265f/180); //convert degrees to radians
  const double sinA = sin(hue_coeff*3.14159265f/180); //convert degrees to radians
  const double rwgt = 0.3086;
  const double gwgt = 0.6094;
  const double bwgt = 0.0820;

  double white_matrix[3][3] = {
    {  1.0,  0.0,  0.0 },
    {  0.0,  1.0,  0.0 },
    {  0.0,  0.0,  1.0 },
  };

  double sat[3][3];
  double m1[3][3];
  double m2[3][3];

  //Compute HUE transform matrix
  double hue_matrix[3][3] = {{cosA + (1.0f - cosA) / 3.0f, 1.0f/3.0f * (1.0f - cosA) - sqrtf(1.0f/3.0f) * sinA, 1.0f/3.0f * (1.0f - cosA) + sqrtf(1.0f/3.0f) * sinA},
    {1.0f/3.0f * (1.0f - cosA) + sqrtf(1.0f/3.0f) * sinA, cosA + 1.0f/3.0f*(1.0f - cosA), 1.0f/3.0f * (1.0f - cosA) - sqrtf(1.0f/3.0f) * sinA},
    {1.0f/3.0f * (1.0f - cosA) - sqrtf(1.0f/3.0f) * sinA, 1.0f/3.0f * (1.0f - cosA) + sqrtf(1.0f/3.0f) * sinA, cosA + 1.0f/3.0f * (1.0f - cosA)}};

  //Compute white balance transform matrix
  white_matrix[0][0] = 1.0 + white_coeff;
  if(white_matrix[0][0] > 1.0)
    white_matrix[0][0] = 1.0;

  white_matrix[2][2] = 1.0 - white_coeff;
  if(white_matrix[2][2] > 1.0)
    white_matrix[2][2] = 1.0;

  //m1 = multiply color white transform matrix by HUE transform matrix
  for(i = 0;i < 3;i++)
    for(j = 0;j < 3;j++)
    {
      m1[i][j] = 0;
      for(k=0;k<3;k++)
        m1[i][j] += hue_matrix[i][k] * white_matrix[k][j];
    }

  //Compute the saturation matrix
  sat[0][0] = (1.0-sat_r_coeff)*rwgt + sat_r_coeff;
  sat[0][1] = (1.0-sat_r_coeff)*gwgt;
  sat[0][2] = (1.0-sat_r_coeff)*bwgt;

  sat[1][0] = (1.0-sat_g_coeff)*rwgt;
  sat[1][1] = (1.0-sat_g_coeff)*gwgt + sat_g_coeff;
  sat[1][2] = (1.0-sat_g_coeff)*bwgt;

  sat[2][0] = (1.0-sat_b_coeff)*rwgt;
  sat[2][1] = (1.0-sat_b_coeff)*gwgt;
  sat[2][2] = (1.0-sat_b_coeff)*bwgt + sat_b_coeff;

  //m2 = multiply saturation matrix by m1 transform matrix
  for(i = 0;i < 3;i++)
    for(j = 0;j < 3;j++)
    {
      m2[i][j] = 0;
      for(k=0;k<3;k++)
        m2[i][j] += m1[i][k] * sat[k][j];
    }

  //Generate the color correction matrix
  for(i = 0;i < 3;i++)
  {
    for(j = 0;j < 3;j++)
    {
      color_correction_matrix[i][j] = m2[i][j] * 127.;
    }
  }

  color_correction_matrix[3][0] = 0.;
  color_correction_matrix[3][1] = 0.;
  color_correction_matrix[3][2] = 0.;
  color_correction_matrix[4][0] = 1.;
  color_correction_matrix[4][1] = 1.;
  color_correction_matrix[4][2] = 1.;

  int fd;
  fd = open("/dev/fb1",O_RDWR);
  if ( fd < 0 )
    return -1;

  struct mxcfb_csc_matrix csc_matrix;
  memset(&csc_matrix,0,sizeof(csc_matrix));

  for(i = 0;i < 3;i++)
  {
    for(j = 0;j < 3;j++)
    {
      csc_matrix.param[i][j] = (int)color_correction_matrix[i][j] & 0x3FF;
    }
  }
  for(i = 0;i < 3;i++)
  {
    csc_matrix.param[3][i] = (int)color_correction_matrix[3][i] & 0x3FFF;
    csc_matrix.param[4][i] = (int)color_correction_matrix[4][i];
  }

  int retval = ioctl(fd, MXCFB_CSC_UPDATE_LCD, &csc_matrix);
  close(fd);
  if (retval < 0) {
    printf("Ioctl MXCFB_CSC_UPDATE_LCD fail!\n");
    return -1;
  }

  return 0;
}


/***********************************************************************************************************
 Updating the backlight brightness value with the one stored in I2C SEEPROM

 NOTE: Scaling is done to properly map the range [0..255] of the I2C SEEPROM stored value with the range 
       [1..max_brightness], which is the available dynamic range for the backlight driver.
 ***********************************************************************************************************/
void UpdateBrightness()
{
  int max_brightness;
  int target_brightness;

  char strval[5]={0,0,0,0,0};
  char brightnessdevice[MAXPATHLENGTH] = BRIGHTNESSDEVICE;

  // Get the full path for accessing the backlight driver: we should have an additonal subdir to be appended to the hardcoded path
  DIR           *d;
  struct dirent *dir;
  d = opendir(BRIGHTNESSDEVICE);
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
      if(dir->d_name[0] != '.')
      {
	strcat(brightnessdevice, dir->d_name);
	strcat(brightnessdevice,"/");
	break;
      }
    }
    closedir(d);
  }

  // Read the max_brightness value for the backlight driver and perform sanity check
  sysfs_read(brightnessdevice,"max_brightness",strval,3);
  max_brightness=atoi(strval);

  if((max_brightness < 1) || (max_brightness > 255))
    max_brightness = 100;

  // Read the target brightness from SEEPROM and perform scaling to suit the dynamic range of the backlight driver
  target_brightness = get_brightness_from_seeprom();

  if ( IS_US03(gethwcode()) ) {
    if (target_brightness < MAX_GAMMA_LEVEL) {
      // apply gamma correction
      applyGammaDimming(target_brightness);
      target_brightness = 1;
    } else {
      applyGammaDimming(MAX_GAMMA_LEVEL);
      target_brightness -= MAX_GAMMA_LEVEL;
      target_brightness = ceil((target_brightness * max_brightness)/(255.0 - MAX_GAMMA_LEVEL));
    }
  } else {
    target_brightness = ceil((target_brightness * max_brightness)/(255.0));
  }

  if(target_brightness > max_brightness)
    target_brightness = max_brightness;

  if(target_brightness < 1)
    target_brightness = 1;

  // Transition loop to set the actual brightness value
  int usdelay = 1000000 / target_brightness;
  int i;

  for(i=1; i < target_brightness; i++)
  {
    SetBrightness(brightnessdevice, & i);
    usleep(usdelay);
  }

  SetBrightness(brightnessdevice, &target_brightness);
}

/***********************************************************************************************************
 Opening the touchscreen event for reading.

 ret = int touch_fd = file descriptor (< 0 if error)
 
 NOTE: The touch event can be defined by the "TSDEVICE" environment var. If TSDEVICE not defined, the
       default "/dev/input/event0" or  "/dev/input/event1" is used, based on the hw_code (taken from cmdline)
       hw_code=110 -> "/dev/input/event0" (This is the ECO panel, which uses the CPU touch controller)
       hw_code=124,125,122,121 -> "/dev/input/event2" (jSmart, Wu16+uS03, Wu16+uS01)
       hw_code=... -> "/dev/input/event1"
 ***********************************************************************************************************/
int Touch_open()
{
  int touch_fd = -1;
  char *tsdevice = NULL;

  if( (tsdevice = getenv("TSDEVICE")) != NULL )
  {
    touch_fd = open(tsdevice,O_RDONLY | O_NONBLOCK);
  }
  else
  {
    int hw_code = gethwcode();
    int touch_type = gettouchtype();

    switch( hw_code )
    {
	case ECO_VAL:
	case BE15A_VAL:
	case BE15B_VAL:
	case PGDXCA16_VAL:
	case AB19_VAL:
	    touch_fd = open(DEFAULT_TOUCH_EVENT0,O_RDONLY | O_NONBLOCK);
	    break;

	case PGDXCA18_VAL:
	    /* CA18 could be have 2 types of Touchscreen analog (touch_type=10) or i2c */
            if ( touch_type == 10 )
	        touch_fd = open(DEFAULT_TOUCH_EVENT2,O_RDONLY | O_NONBLOCK);
	    else
		touch_fd = open(DEFAULT_TOUCH_EVENT0,O_RDONLY | O_NONBLOCK);
	    break;

	case WU16_VAL:
	case US03WU16_VAL:
	case EX8XX_VAL:
	    touch_fd = open(DEFAULT_TOUCH_EVENT2,O_RDONLY | O_NONBLOCK);
	    break;

	case AUTEC_VAL:
	    touch_fd = open(DEFAULT_TOUCH_EVENT1,O_RDONLY | O_NONBLOCK);
	    if( touch_fd < 0 ){
			touch_fd = open(DEFAULT_TOUCH_EVENT0,O_RDONLY | O_NONBLOCK);
	    }
	    break;

	default:
	    touch_fd = open(DEFAULT_TOUCH_EVENT1,O_RDONLY | O_NONBLOCK);
	    break;
    }
  }
  
  if(touch_fd < 0)
    fprintf(stderr, "psplash: Error opening the touch event: err=%s\n", strerror(errno));
  
  return touch_fd;
}


/***********************************************************************************************************
 Closing the touchscreen file descriptor.
 ***********************************************************************************************************/
void Touch_close(int touch_fd)
{
  if(touch_fd < 0)
    return;
  
  close(touch_fd);
}


/***********************************************************************************************************
 Touch handler: counts the number of tap-tap events detected and gets the last detected touch status.
 
 int  touch_fd   (file descriptor to touchscreen event)
 int* taptap     (taptap detected number)
 int* laststatus (0=up, 1=pressed)
 
 int ret = number of detected UP/DOWN events (0=nothing happened)
 ***********************************************************************************************************/
int Touch_handler(int touch_fd, int* taptap, int* laststatus)
{
  struct input_event ev;
  fd_set fdset;
  struct timeval tv;
  int count = 0;
  int nfds;
  int ret;
  
  if(touch_fd < 0)
    return 0;
  
  while (1) 
  {
    FD_ZERO(&fdset);
    FD_SET(touch_fd, &fdset);
    
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    
    nfds = select(touch_fd + 1, &fdset, NULL, NULL, &tv);
    if (nfds == 0) 
      break;
    
    ret = read(touch_fd, &ev, sizeof(struct input_event));
    if (ret < (int)sizeof(struct input_event)) 
    {
      break;
    }
    
    if (ev.type == EV_KEY)
      if((ev.code == BTN_TOUCH) || (ev.code == BTN_LEFT))
      {
	if(ev.value == 0)
	{ //pen UP 
	  *taptap = *taptap + 1;
	  *laststatus = 0;
	  count++;
	}
	else if(ev.value == 1)
	{ //pen DW
	  *laststatus = 1;
	  count++;
	}
      }
  }
  
  return count;
}


/***********************************************************************************************************
 TapTap_Progress: Updates the tap-tap counting on display, to give a visual feedback to the user

 PSplashFB* fb   (pointer to framebuffer structure)
 int taptap      (taptap detected number)
 ***********************************************************************************************************/
void TapTap_Progress(PSplashFB *fb, int taptap)
{
  extern void  psplash_draw_msg (PSplashFB *fb, const char *msg);
  
  // Exit in case the actual taptap counter is still too low
  if(taptap <= TAPTAP_THLO)
    return;

  // Build a string representing the actual TAP-TAP counter value
  char msg[MAXPATHLENGTH] = "";
  int i;
  
  for(i=0; i<TAPTAP_TH; i++)
  {
    if(i<taptap)
      strcat(msg,"#");
    else
      strcat(msg,".");
  }
  
  // Draw the string
  psplash_draw_msg (fb, msg);
}


/***********************************************************************************************************
 TapTap_Detected: Handles the sequence for deciding what to do when TAP-TAP detected.

 PSplashFB* fb   (pointer to framebuffer structure)
 int laststatus  (last touchscreen status)

 output: int ret (1=exit, 0=continue)
 ***********************************************************************************************************/
int TapTap_Detected(int touch_fd, PSplashFB *fb, int laststatus)
{
  extern void  psplash_draw_msg (PSplashFB *fb, const char *msg);
  extern int reboot(int cmd);
  
  int refreshtrigger = 0xff;
  int time; //Time [s/200]
  char msg[MAXPATHLENGTH];
  int taptap = 0;
  int prevstatus = laststatus;
  
  // Perform synchronization with the JMlauncher: put it in wait status
  SyncJMLauncher("wait");
  
  // Perform countdown, touch status reading and msg updating, based on touch status (pressed or not pressed)
  for(time = 1000; time > 0; time -= 50)
  {
    if((time%200) == 0)    //refresh printout at least every second
      refreshtrigger = 0xff;
    
    Touch_handler(touch_fd, &taptap, &laststatus);
    if(prevstatus != laststatus)
       time = 1000;
    prevstatus = laststatus;    
	
    if(laststatus != refreshtrigger)
    { //It is time to refresh the printout
      refreshtrigger = laststatus;
      if(laststatus) 
      {
	sprintf(msg, "** TAP-TAP DETECTED  %d **\n>> RESTART: CONFIG OS\n   SYSTEM SETTINGS\n",(int)(time/200));
	Draw_Icon(fb, CONFIGOS_IMG_WIDTH, CONFIGOS_IMG_HEIGHT, CONFIGOS_IMG_RLE_PIXEL_DATA, PSPLASH_TEXTBK_COLOR);
      }
      else
      {
	sprintf(msg, "** TAP-TAP DETECTED  %d **\n   RESTART: CONFIG OS\n>> SYSTEM SETTINGS\n",(int)(time/200));
	Draw_Icon(fb, SETTINGS_IMG_WIDTH, SETTINGS_IMG_HEIGHT, SETTINGS_IMG_RLE_PIXEL_DATA, PSPLASH_TEXTBK_COLOR);
      }
      // Draw the string
      psplash_draw_msg (fb, msg);
    }
    usleep(200000);
  }
  
  // Now, based on the touchscreen laststatus (pressed or not pressed) the proper action will be taken ...
  if(laststatus)
  { // In this case we will restart the recovery OS
    sprintf(msg, "** TAP-TAP DETECTED  %d **\n\nRESTARTING: CONFIG OS ...\n",(int)(time/200));
    psplash_draw_msg (fb, msg);
    Draw_Icon(fb, CONFIGOS_IMG_WIDTH, CONFIGOS_IMG_HEIGHT, CONFIGOS_IMG_RLE_PIXEL_DATA, 0xff, 0xff, 0x00);
    usleep(3000000);
    
    // The recovery OS is forced to boot by setting the bootcounter over the threshold limit
    setbootcounter(100); 
    //Now perform reboot unconditionally (please note the JMloader is still kept into the "wait" status, so it will not try booting anything in the meanwhile)
    #define LINUX_REBOOT_CMD_RESTART        0x01234567
    sync();
    reboot(LINUX_REBOOT_CMD_RESTART);    
    //We should never get here !!!
    while(1)
      usleep(200);
  }
  else
  {
    Draw_Icon(fb, SETTINGS_IMG_WIDTH, SETTINGS_IMG_HEIGHT, SETTINGS_IMG_RLE_PIXEL_DATA, 0xff, 0xff, 0x00);
    usleep(300000);
    
    // In this case we will inform the JMloader to start the system settings menu by setting the "disable-kiosk" or "disable-kiosk-tchcalibrate" status, then we will normally exit
    // Perform countdown, touch status reading and msg updating, based on touch status (pressed or not pressed)
    for(time = 1000; time >= 0; time -= 50)
    {
      if((time%200) == 0)    //refresh printout at least every second
	refreshtrigger = 0xff;
      
      Touch_handler(touch_fd, &taptap, &laststatus);
      if(prevstatus != laststatus)
         time = 1000;
      prevstatus = laststatus;    

      if(laststatus != refreshtrigger)
      { //It is time to refresh the printout
	refreshtrigger = laststatus;
	if(!laststatus) 
	{
	  sprintf(msg, "** ENTERING SYSTEM SETTINGS  %d **\n>> DEFAULT MODE\n   TOUCHSCREEN CALIBRATION\n",(int)(time/200));
	  Draw_Icon(fb, SETTINGS_IMG_WIDTH, SETTINGS_IMG_HEIGHT, SETTINGS_IMG_RLE_PIXEL_DATA, PSPLASH_TEXTBK_COLOR);
	}
	else
	{
	  sprintf(msg, "** ENTERING SYSTEM SETTINGS  %d **\n   DEFAULT MODE\n>> TOUCHSCREEN CALIBRATION\n",(int)(time/200));
	  Draw_Icon(fb, CALIB_IMG_WIDTH, CALIB_IMG_HEIGHT, CALIB_IMG_RLE_PIXEL_DATA, PSPLASH_TEXTBK_COLOR);
	}
	// Draw the string
	psplash_draw_msg (fb, msg);
      }
      usleep(200000);
    }
    
    //If the touch calibration was selected and we have a Jsmart, draw message that the touch calibration was done automatically.
    if(laststatus)
    {
      int hw_code = gethwcode();
      if((hw_code == JSMART_VAL) || (hw_code == JSMARTQ_VAL) || (hw_code == JSMARTTTL_VAL))
      {
	sprintf(msg, "** ENTERING SYSTEM SETTINGS  %d **\nTOUCHSCREEN CALIBRATION\nAUTOMATICALLY DONE.\n",(int)(0));
	psplash_draw_msg (fb, msg);
      }
    }
    // highlight icon for the selected option
    if(!laststatus)
      Draw_Icon(fb, SETTINGS_IMG_WIDTH, SETTINGS_IMG_HEIGHT, SETTINGS_IMG_RLE_PIXEL_DATA, 0xff, 0xff, 0x00);
    else
      Draw_Icon(fb, CALIB_IMG_WIDTH, CALIB_IMG_HEIGHT, CALIB_IMG_RLE_PIXEL_DATA, 0xff, 0xff, 0x00);
      
    // Perform synchronization with the JMlauncher based on the user's choice
    usleep(3000000);
    if(!laststatus)
      SyncJMLauncher("disable-kiosk");
    else
      SyncJMLauncher("disable-kiosk-tchcalibrate");
  }
  return 0;
}
