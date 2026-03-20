#ifndef __CALCULATIONS_H__
#define __CALCULATIONS_H__
#include "data/electricity_structs.h"
#include "sqlite_helpers.h"

typedef struct
{
  const char* calcs_dir;
  const char* data_dir;
  const char* weather_dir;
  SqlHelper* sqlhelper;
  SpotPriceClass price_class;
  SpotCurrency currency;
  int max_threads;
  int panel_size;

  double latitude;
  double longitude;
  int panel_tilt;
  unsigned int panel_azimuth;
  bool forecast;

} Calc_Args;

/* ======================= INTERFACE ======================= */

int calc_create_reports(Calc_Args* _Args);

/* ========================================================= */

#endif
