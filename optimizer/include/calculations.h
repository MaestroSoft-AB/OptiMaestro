#ifndef __CALCULATIONS_H__
#define __CALCULATIONS_H__
#include "data/electricity_structs.h"

typedef struct
{
  const char* calcs_dir;
  const char* data_dir;
  const char* weather_dir;
  SpotPriceClass price_class;
  SpotCurrency currency;
  int max_threads;
  int panel_size;

} Calc_Args;

/* ======================= INTERFACE ======================= */

int calc_create_reports(Calc_Args* _Args);

/* ========================================================= */

#endif
