#ifndef __CALCULATIONS_H__
#define __CALCULATIONS_H__
#include "data/electricity_structs.h"

typedef enum
{
  ABOVE_AVG = -1,
  AVG = 0,
  BELOW_AVG = 1

} Price_Avg;

typedef struct
{
  const char*    calcs_dir;
  const char*    spots_dir; 
  const char*    weather_dir; 
  SpotPriceClass price_class; 
  SpotCurrency   currency; 
  int            max_threads; 
  int            panel_size;

} Calc_Args;

int calc_create_reports(Calc_Args* _Args);

int print_averages(const Electricity_Spots* s, const char* _calcs_dir);

#endif
