#include "calculations.h"
#include "electricity_cache_handler.h"
#include <maestroutils/error.h>
#include <maestroutils/file_logging.h>
#include <stdio.h>
#include <time.h>
#define BASE_CACHE_PATH "/var/lib/maestro/spots"
#define AVG_CACHE_PATH "/var/lib/maestro/"
enum
{
  INTERVAL_W = 43,
  PRICE_W = 12,
  DEV_W = 8,
  STATUS_W = 9
};

int init_calc_ech(ECH* ech);

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
  FILE* f;
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y%m%d", &tm);

  char filename_96[128];
  char filename_24[128];

  int res = snprintf(filename_96, sizeof(filename_96), "%s%s-SP96-SE1.json", AVG_CACHE_PATH, today);

  if (res < 0 || (size_t)res >= sizeof(filename_96)) {
    printf("Failed to create filename_96");
    return ERR_NO_MEMORY;
  }

  f = fopen(filename_96, "w");

  if (!f) {
    perror("fopen");
    return ERR_IO;
  }

  char start_buf[32];
  char end_buf[32];
  double daily_avg = calc_avg(s);

  fprintf(f, "\n================ DAILY PRICE SUMMARY ================\n");
  fprintf(f, "Daily Average: %.4f\n", daily_avg);

  fprintf(f, "--------------------------------------------------------------------------------\n");
  fprintf(f, "%-*s | %*s | %*s | %-*s\n", INTERVAL_W, "Interval", PRICE_W, "Price/kw", DEV_W,
          "Dev %", STATUS_W, "Status");
  fprintf(f, "--------------------------------------------------------------------------------\n");

  for (int i = 0; i < s->price_count; i++) {

    struct tm tm_start, tm_end;
    struct tm* tmp;

    tmp = localtime(&s->prices[i].time_start);
    if (tmp)
      tm_start = *tmp;

    tmp = localtime(&s->prices[i].time_end);
    if (tmp)
      tm_end = *tmp;

    strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H:%M:%S", &tm_start);
    strftime(end_buf, sizeof(end_buf), "%Y-%m-%d %H:%M:%S", &tm_end);

    double price = (double)s->prices[i].spot_price;
    double deviation = ((price - daily_avg) / daily_avg) * 100.0;

    const char* status;

    if (price > daily_avg * 1.1)
      status = "EXPENSIVE";
    else if (price < daily_avg * 0.9)
      status = "CHEAP";
    else
      status = "AVERAGE";

    fprintf(f, "%19s - %19s%*s | %*.3f SEK | %+*.2f | %-*s\n", start_buf, end_buf,
            (int)(INTERVAL_W - 41), "", PRICE_W - 4, price, /* -4 för " SEK" */
            DEV_W, deviation, STATUS_W, status);
  }

  fprintf(f,
          "--------------------------------------------------------------------------------\n\n");

  fclose(f);

  res = snprintf(filename_24, sizeof(filename_24), "%s%s-SP24-SE1.json", AVG_CACHE_PATH, today);

  if (res < 0 || (size_t)res >= sizeof(filename_24)) {
    printf("Failed to create filename_24");
    return ERR_NO_MEMORY;
  }

  f = fopen(filename_24, "w");

  enum
  {
    TIME_W = 19,
    AVG_W = 12,
    DEV_W = 8
  };

  fprintf(f, "\n================ HOURLY AVERAGES =====================\n");
  fprintf(f, "Daily Average: %.4f\n", daily_avg);

  fprintf(f, "------------------------------------------------------\n");
  fprintf(f, "%-*s | %*s | %*s\n", TIME_W, "Hour starting", AVG_W, "Hour Avg", DEV_W, "Dev %");
  fprintf(f, "------------------------------------------------------\n");

  for (int i = 0; i < s->price_count; i += 4) {

    double hour_sum = 0.0;
    int count = 0;

    for (int j = 0; j < 4 && (i + j) < s->price_count; j++) {
      hour_sum += s->prices[i + j].spot_price;
      count++;
    }

    double hour_avg = hour_sum / count;
    double deviation = ((hour_avg - daily_avg) / daily_avg) * 100.0;

    char start_buf[20];
    struct tm tm_start;
    struct tm* tmp;

    tmp = localtime(&s->prices[i].time_start);
    if (tmp)
      tm_start = *tmp;

    strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H:%M:%S", &tm_start);

    fprintf(f, "%-*s | %*.3f SEK | %+*.2f\n", TIME_W, start_buf, AVG_W - 4,
            hour_avg, /* -4 för " SEK" */
            DEV_W, deviation);
  }

  fprintf(f, "------------------------------------------------------\n\n");

  fclose(f);
  return 0;
}

int calc_get_average(void)
{

  ECH_Conf* conf = malloc(sizeof(ECH_Conf));
  conf->currency = SPOT_SEK;
  conf->data_dir = "/var/lib/maestro/spots";
  conf->price_class = SE1;

  ECH* ech = malloc(sizeof(ECH));
  if (!ech) {
    return ERR_NO_MEMORY;
  }
  ech->conf = *conf;

  init_calc_ech(ech);

  printf("Ech cache path in calc: %s\n", ech->cache_path);

  int res = ech_read_cache(&ech->spot, ech->cache_path);
  if (res != 0) {
    LOG_ERROR("ech_read_cache (%i)", res); // TODO: Logger
    return res;
  }

  print_averages(&ech->spot);

  ech_dispose(ech);

  return 0;
}

int init_calc_ech(ECH* ech)
{
  if (!ech) {
    return ERR_INVALID_ARG;
  }

  memset(&ech->spot, 0, sizeof(Electricity_Spots));
  memset(&ech->epjn_spot, 0, sizeof(EPJN_Spots));

  ech->spot.price_class = ech->conf.price_class;
  ech->spot.currency = ech->conf.currency;

  time_t today = epoch_now_day();

  printf("Ech->conf-data-dir: %s\n", ech->conf.data_dir);

  ech->cache_path =
      ech_get_cache_filepath(ech->conf.data_dir, today, ech->conf.price_class, ech->conf.currency);

  return SUCCESS;
}
