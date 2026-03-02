#include "calculations.h"
#include <maestroutils/error.h>
#include <stdio.h>
#include <time.h>
#define BASE_CACHE_PATH "/var/lib/maestro/spots"
double calc_avg(const Electricity_Spots* s)
{
  if (!s) {
    return ERR_INVALID_ARG;
  }

  double sum = 0.0f;

  for (int i = 0; i < s->price_count; i++) {
    sum += s->prices[i].spot_price;
  }

  return sum / (double)s->price_count;
}

int print_averages(const Electricity_Spots* s)
{
  if (!s) {
    return ERR_INVALID_ARG;
  }

  char start_buf[32];
  char end_buf[32];
  double avg = calc_avg(s);

  for (int i = 0; i < s->price_count; i++) {
    struct tm* tm_start = localtime(&s->prices[i].time_start);
    struct tm* tm_end = localtime(&s->prices[i].time_end);

    strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H:%M:%S", tm_start);
    strftime(end_buf, sizeof(end_buf), "%Y-%m-%d %H:%M:%S", tm_end);

    if (s->prices[i].spot_price > (avg * 1.1f)) {
      printf("Interval: %s - %s | Price %f - PRICE IS TOO DAMN HIGH\n", start_buf, end_buf,
             s->prices[i].spot_price);
    } else if (s->prices[i].spot_price < (avg * 0.9f)) {
      printf("Interval: %s - %s | Price %f - Price is low, BUY BUY BUY!\n", start_buf, end_buf,
             s->prices[i].spot_price);
    } else {
      printf("Interval: %s - %s | Price %f - Price is Average!\n", start_buf, end_buf,
             s->prices[i].spot_price);
    }
  }

  return 0;
}
