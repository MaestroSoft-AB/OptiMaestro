#ifndef __CALCULATIONS_H__
#define __CALCULATIONS_H__
#include "data/electricity_structs.h"

typedef enum
{
  ABOVE_AVG = -1,
  AVG = 0,
  BELOW_AVG = 1

} Price_Avg;
int calc_get_average();
int print_averages(const Electricity_Spots* s);

#endif
