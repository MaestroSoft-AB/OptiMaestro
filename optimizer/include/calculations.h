#ifndef __CALCULATIONS_H__
#define __CALCULATIONS_H__
#include "data/electricity_structs.h"

typedef enum
{
  CHEAP = -1,
  AVERAGE = 0,
  EXPENSIVE = 1

} PriceCheapness;

typedef struct
{
  float*           spot_prices;
  float*           spot_prices_deviation; // % deviation from daily avg
  PriceCheapness*  spot_prices_cheapness;

  float*           solar_gains; // How much we gain from solar panels
  float*           solar_gains_deviation; // % deviation from daily avg

  time_t*          timestamps;


  float            cheapness_thresh; // % to consider above/below average in decimals
  float            spot_prices_avg;
  float            solar_gains_avg;

  unsigned int     count;

} Calc_Results;

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
