#ifndef __CALCULATIONS_H__
#define __CALCULATIONS_H__
#include "data/electricity_structs.h"
#include "data/facility.h"

typedef struct
{
  char* calcs_dir;
  char* data_dir;
  char* weather_dir;

  Facility_Config** facility_configs;
  size_t            facility_count;

  // SpotPriceClass price_class;
  // SpotCurrency currency;
  // int panel_size;

  int max_threads;

} Calc_Args;

/* ======================= INTERFACE ======================= */

int calc_create_reports(const Calc_Args* _Args);

/* ========================================================= */

#endif
