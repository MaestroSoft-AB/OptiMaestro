#ifndef __FACILITY_H__
#define __FACILITY_H__

#include "data/electricity_structs.h"

typedef struct
{
  unsigned int    size;
  unsigned short  tilt;
  short           azimuth; // +/- 180
  
} Solar_Panel;

typedef struct
{
  char*           name;
  Solar_Panel*    panel;

  float           lat;
  float           lon;

  SpotCurrency    currency;
  SpotPriceClass  price_class;

} Facility_Config;

Facility_Config** facility_get_configs(const char* _facility_dir, size_t _facility_count);

void facility_dispose(Facility_Config** _Configs, size_t _facility_count);

#endif
