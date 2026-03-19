#ifndef __SQLITE_HELPERS_H__
#define __SQLITE_HELPERS_H__

#include "data/electricity_structs.h"
#include "data/weather_structs.h"
#include <pthread.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <time.h>

typedef struct
{
  sqlite3* db;
  pthread_mutex_t mutex;
} SqlHelper;

int sql_helper_init(SqlHelper* _H);
int sql_helper_open(SqlHelper* _H, const char* _path);
void sql_helper_close(SqlHelper* _H);
int sql_helper_init_schema(SqlHelper* _H);
int sql_helper_insert_spots(SqlHelper* _H, const Electricity_Spots* _spot);
int sql_helper_read_spots(SqlHelper* _H, Electricity_Spots* _out, SpotPriceClass _price_class,
                          SpotCurrency _currency, time_t _start, time_t _end);
int sql_helper_insert_weather(SqlHelper* _H, const Weather* _W, bool _forecast);
int sql_helper_read_weather(SqlHelper* _H, Weather* _out, double _latitude, double _longitude,
                            int _panel_tilt, unsigned int _panel_azimuth, bool _forecast,
                            time_t _start, time_t _end);
void sql_helper_dispose(SqlHelper* _H);
#endif
