#include "calculations.h"
#include "data/electricity_structs.h"
#include "data/weather_structs.h"
#include "electricity_cache_handler.h"
#include "maestroutils/file_utils.h"
#include <maestroutils/error.h>
#include <maestroutils/file_logging.h>
#include <maestromodules/thread_pool.h>
#include <stdio.h>
#include <time.h>
// #define BASE_CACHE_PATH "/var/lib/maestro/spots"
// #define AVG_CACHE_PATH "/var/lib/maestro/"
enum
{
  INTERVAL_W = 43,
  PRICE_W = 12,
  DEV_W = 8,
  STATUS_W = 9
};

typedef struct
{
  Calc_Args*         calc_args;
  Electricity_Spots* spots;
  Weather*           weather;

} Calc_Thread_Args;

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

void calc_averages_15min(void* _context)
{
  if (!_context)
    return;

  Calc_Thread_Args* Task_Args = (Calc_Thread_Args*)_context;
  Calc_Args* Calc_Args = Task_Args->calc_args;
  Electricity_Spots* s = Task_Args->spots;

  if (!s || !Calc_Args || !Calc_Args->calcs_dir) {
    return;
  }

  FILE* f;
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y%m%d", &tm);

  char filename_96[128];

  int res = snprintf(filename_96, sizeof(filename_96), "%s/%s-SP96-SE%i.json", Calc_Args->calcs_dir, today, Calc_Args->price_class+1);

  if (res < 0 || (size_t)res >= sizeof(filename_96)) {
    LOG_ERROR("Failed to create filename_96");
    return;
  }

  f = fopen(filename_96, "w");

  if (!f) {
    LOG_ERROR("fopen");
    return;
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

}

void calc_averages_1hour(void* _context)
{
  if (!_context)
    return;

  Calc_Thread_Args* Task_Args = (Calc_Thread_Args*)_context;
  Calc_Args* Calc_Args = Task_Args->calc_args;
  Electricity_Spots* s = Task_Args->spots;

  if (!s || !Calc_Args || !Calc_Args->calcs_dir) {
    return;
  }

  int res;
  FILE* f;
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  char today[11]; // YYYY-MM-DD
  strftime(today, sizeof(today), "%Y%m%d", &tm);

  char filename_24[128];

  res = snprintf(filename_24, sizeof(filename_24), "%s/%s-SP24-SE%i.json", Calc_Args->calcs_dir, today, Calc_Args->price_class+1);

  if (res < 0 || (size_t)res >= sizeof(filename_24)) {
    LOG_ERROR("Failed to create filename_24");
    return;
  }

  f = fopen(filename_24, "w");

  enum
  {
    TIME_W = 19,
    AVG_W = 12,
    DEV_W = 8
  };

  double daily_avg = calc_avg(s);

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
}

int calc_create_reports(Calc_Args* _Args)
{
  if (!_Args->calcs_dir || !_Args->spots_dir || !_Args->weather_dir)
    return ERR_INVALID_ARG;

  create_directory_if_not_exists(_Args->calcs_dir);

  if (_Args->max_threads < 1)
    _Args->max_threads = 1;

  int res;

  // printf("Panel size: %i, Currency; %i, Price class: %i, max threads: %i\n", _panel_size, _Currency, _PriceClass, _max_threads);
  // printf("calcs dir: %s, spots dir: %s, weather dir: %s\n", _calcs_dir, _spots_dir, _weather_dir);

  Calc_Thread_Args Thread_Args = {0};
  Thread_Args.calc_args = _Args;

  Thread_Pool* TP = tp_init(_Args->max_threads);
  if (!TP) {
    LOG_ERROR("tp_init"); // TODO: Logger
    return ERR_FATAL;
  }

  ECH_Conf conf = {0};
  ECH_Conf* conf_ptr = &conf;
  conf_ptr->currency = _Args->currency;
  conf_ptr->data_dir = _Args->spots_dir;
  conf_ptr->price_class = _Args->price_class;

  ECH ech = {0};
  ech.conf = conf;
  ECH* ech_ptr = &ech;

  init_calc_ech(&ech);

  res = ech_read_cache(&ech_ptr->spot, ech_ptr->cache_path);
  if (res != 0) {
    LOG_ERROR("ech_read_cache (%i)", res); // TODO: Logger
    ech_dispose(ech_ptr);
    return res;
  }

  Thread_Args.spots = &ech_ptr->spot;

  TP_Task Avg_Q_Task = {calc_averages_15min, &Thread_Args, NULL, NULL};
  TP_Task Avg_H_Task = {calc_averages_1hour, &Thread_Args, NULL, NULL};

  res = tp_task_add(TP, &Avg_H_Task);
  if (res != 0)
    LOG_ERROR("tp_task_add"); // TODO: Logger

  res = tp_task_add(TP, &Avg_Q_Task);
  if (res != 0)
    LOG_ERROR("tp_task_add"); // TODO: Logger

  tp_wait(TP);
  tp_dispose(TP);

  ech_dispose(ech_ptr);

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

  ech->cache_path =
      ech_get_cache_filepath(ech->conf.data_dir, today, ech->conf.price_class, ech->conf.currency);

  return SUCCESS;
}
