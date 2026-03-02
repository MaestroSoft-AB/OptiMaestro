#ifndef __CALCULATIONS_H__
#define __CALCULATIONS_H__
#include "data/electricity_structs.h"
#include "electricity_cache_handler.h"

typedef enum
{
  ABOVE_AVG = -1,
  AVG = 0,
  BELOW_AVG = 1

} Price_Avg;

int print_averages(const Electricity_Spots* s);
int calc_get_average();

#endif
