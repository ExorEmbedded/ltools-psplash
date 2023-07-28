#include "common.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

static int setbootcounter_emmc(unsigned char val);
static int setbootcounter_nvram(unsigned char val);

/* Safe version of atoi() that detects conversion errors - returns 0 on success, non-zero on failure */
int atoi_s(char *s, int *val)
{
        long l;
        char *endptr;

        errno = 0;
        l = strtol(s, &endptr, 10);
        if (!endptr || endptr == s || *endptr != '\0' ||
                        (errno == ERANGE && (l == LONG_MAX || l == LONG_MIN))) {
                (void) fprintf(stderr, "atoi_s() failed conversion on: %s\n", s);
                return ~0;
        }
        if (l > INT_MAX || l < INT_MIN) {
                (void) fprintf(stderr, "atoi_s() not an integer: %s\n", s);
                return ~0;
        }

        *val = (int) l;

        return 0;
}

int gethwcode()
{
  static int hw_code = -1;

  // return cached value if already called
  if (hw_code != -1)
      return hw_code;

  int hc = -1;
  char cmdline[MAXPATHLENGTH];

  memset(cmdline, 0, MAXPATHLENGTH);
  if (sysfs_read(CMDLINEPATH,"cmdline",cmdline,MAXPATHLENGTH-1))
    return -1;
  char * pch;

  pch = strstr(cmdline, "hw_code=") + strlen("hw_code=");
  if (pch == NULL)
    return -1;

  // NOLINTNEXTLINE(cert-err34-c) safe because argument comes from sysfs
  if (sscanf (pch,"%d %*s", &hc) < 1)
    return -1;

  hw_code = hc;
  fprintf(stderr, "Detected hw_code: %d\n", hw_code);

  return hw_code;
}

int gettouchtype()
{
  static int touch_type = -1;

  // return cached value if already called
  if (touch_type != -1)
      return touch_type;

  int hc = -1;
  char cmdline[MAXPATHLENGTH];

  memset(cmdline, 0, MAXPATHLENGTH);
  if (sysfs_read(CMDLINEPATH,"cmdline",cmdline,MAXPATHLENGTH-1))
    return -1;
  char * pch;

  pch = strstr(cmdline, "touch_type=") + strlen("touch_type=");
  if (pch == NULL)
    return -1;

  // NOLINTNEXTLINE(cert-err34-c) safe because argument comes from sysfs
  if (sscanf (pch,"%d %*s", &hc) < 1)
    return -1;

  touch_type = hc;
  fprintf(stderr, "Detected touch_type: %d\n", touch_type);

  return touch_type;
}

int setbootcounter(unsigned char val)
{
  int hw_code = gethwcode();

  switch (hw_code)
  {
    case PGDXCA16_VAL:
    case PGDXCA18_VAL:
    case PGDXCA7LE_VAL:
      fprintf(stderr, "Setting boot counter %d in eMMC\n", val);
      return setbootcounter_emmc(val);
    default:
      fprintf(stderr, "Setting boot counter %d in NVRAM\n", val);
      return setbootcounter_nvram(val);
  }
}

/***********************************************************************************************************
 Set the bootcounter (in eMMC) to the specified value
 ***********************************************************************************************************/
#define BOOT1DEVICE                      "/dev/mmcblk1boot1"
#define BOOT1ROSYSFS                     "/sys/block/mmcblk1boot1/force_ro"
static int setbootcounter_emmc(unsigned char val)
{
  #define BC_MAGIC 0xbc
  unsigned char buff[2];

  buff[0] = BC_MAGIC;
  buff[1] = val;

  FILE* sysfs_ro = fopen(BOOT1ROSYSFS, "w");
  FILE* fp = fopen(BOOT1DEVICE, "wb");

  if(fp == NULL || sysfs_ro == NULL)
  {
    fprintf(stderr,"setbootcounter cannot open file -> %s \n",BOOT1DEVICE);
    goto err;
  }

  if (fseek(fp, 0x80000, SEEK_SET))
  {
    fprintf(stderr,"setbootcounter cannot open file -> %s \n",BOOT1DEVICE);
    goto err;
  }

  if (fputs("0", sysfs_ro) == EOF) {
    fprintf(stderr,"setbootcounter failed making writeable\n");
    goto err;
  }
  (void) fflush(sysfs_ro);

  if (fwrite(buff,1,2,fp) < 2) {
    fprintf(stderr,"setbootcounter failed write\n");
    (void) fclose(fp); fp = NULL;
    goto err;
  }
  (void) fflush(fp);
  (void) fclose(fp); fp = NULL;

  if (fputs("1", sysfs_ro) == EOF) {
    fprintf(stderr,"setbootcounter failed making readonly\n");
    goto err;
  }
  (void) fflush(sysfs_ro);
  (void) fclose(sysfs_ro); sysfs_ro = NULL;

  return 0;
err:
  if (fp)
      (void) fclose(fp);
  if (sysfs_ro)
      (void) fclose(sysfs_ro);
  return -1;
}

/***********************************************************************************************************
 Set the bootcounter (in NVRAM) to the specified value
 ***********************************************************************************************************/
#define NVRAMDEVICE                      "/sys/class/rtc/rtc0/device/nvram"
static int setbootcounter_nvram(unsigned char val)
{
  #define NVRAM_MAGIC 0xbc
  unsigned char buff[2];
  
  buff[0] = NVRAM_MAGIC;
  buff[1] = val;
  
  // Opens the nvram device and updates the bootcounter
  FILE* fp;
  if((fp = fopen(NVRAMDEVICE, "w"))==NULL)
  {
    fprintf(stderr,"setbootcounter cannot open file -> %s \n",NVRAMDEVICE);
    return -1;
  }
  
  if (fwrite(buff,1,2,fp) < 2) {
    fprintf(stderr,"setbootcounter failed write\n");
    (void) fclose(fp);
    return -1;
  }
  (void) fflush(fp); //Just in case ... but not really needed
  (void) fclose(fp);
  
  return 0;
}

//Helper function for reading a parameter from sysfs
int sysfs_read(char* pathto, char* fname, char* value, int n)
{
  char str[MAXPATHLENGTH];
  FILE* fp;
  
  snprintf(str, sizeof(str), "%s%s", pathto, fname);
  
  if((fp = fopen(str, "rb"))==NULL)
  {
    fprintf(stderr,"Cannot open sysfs file -> %s \n",str);
    return -1;
  }
  
  rewind(fp);
  if (fread (value, 1, n, fp) != n) {
    (void) fclose(fp);
    return -1;
  }

  // trim newline
  if (value[n-1] == '\n')
    value[n-1] = '\0';

  (void) fclose(fp);
  return 0;
}

//Helper function for writing a parameter to sysfs
int sysfs_write(char* pathto, char* fname, char* value)
{
  char str[MAXPATHLENGTH];
  FILE* fp;
  
  size_t value_sz = strlen(value);
  snprintf(str, sizeof(str), "%s%s", pathto, fname);
  
  if((fp = fopen(str, "ab"))==NULL)
  {
    fprintf(stderr,"Cannot open sysfs file -> %s \n",str);
    return -1;
  }
  
  rewind(fp);
  if (fwrite(value,1,value_sz,fp) < value_sz) {
    fprintf(stderr,"sysfs failed write\n");
    (void) fclose(fp);
    return -1;
  }
  (void) fclose(fp);
  return 0;
}
