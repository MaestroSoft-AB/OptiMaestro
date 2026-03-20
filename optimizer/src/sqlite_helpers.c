#define _POSIX_C_SOURCE 200809L
#include "sqlite_helpers.h"
#include <maestroutils/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQL_CHECK_DB(_db, _msg) fprintf(stderr, "[SQL] %s: %s\n", _msg, sqlite3_errmsg(_db))

int sql_helper_init(SqlHelper* _H)
{
  if (!_H) {
    return ERR_INVALID_ARG;
  }

  _H->db = NULL;

  if (pthread_mutex_init(&_H->mutex, NULL) != 0) {
    return ERR_FATAL;
  }

  return SUCCESS;
}

int sql_helper_open(SqlHelper* _H, const char* _path)
{
  if (!_H || !_path) {
    return ERR_INVALID_ARG;
  }

  pthread_mutex_lock(&_H->mutex);

  if (sqlite3_open(_path, &_H->db) != SQLITE_OK) {
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }
  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}

int sql_helper_init_schema(SqlHelper* _H)
{
  if (!_H) {
    return ERR_INVALID_ARG;
  }

  const char* sql =
      /* Electricity spots */
      "CREATE TABLE IF NOT EXISTS electricity_spots ("
      " time_start INTEGER NOT NULL,"
      " time_end INTEGER NOT NULL,"
      " spot_price REAL NOT NULL,"
      " price_class INTEGER NOT NULL,"
      " currency INTEGER NOT NULL,"
      " PRIMARY KEY (time_start, price_class, currency)"
      ");"

      "CREATE INDEX IF NOT EXISTS idx_spots_facility_time "
      "ON electricity_spots(price_class, currency, time_start);"

      /* Weather meta/config */
      "CREATE TABLE IF NOT EXISTS facility ("
      " id INTEGER PRIMARY KEY AUTOINCREMENT,"
      " latitude REAL NOT NULL,"
      " longitude REAL NOT NULL,"
      " panel_tilt INTEGER NOT NULL,"
      " panel_azimuth INTEGER NOT NULL,"
      " forecast INTEGER NOT NULL,"
      " interval_minutes INTEGER NOT NULL,"
      " temperature_unit TEXT,"
      " windspeed_unit TEXT,"
      " precipitation_unit TEXT,"
      " winddirection_unit TEXT,"
      " radiation_unit TEXT,"
      " UNIQUE(latitude, longitude, panel_tilt, panel_azimuth, forecast)"
      ");"

      /* Weather values */
      "CREATE TABLE IF NOT EXISTS weather_values ("
      " facility_id INTEGER NOT NULL,"
      " timestamp INTEGER NOT NULL,"
      " temperature REAL,"
      " windspeed REAL,"
      " winddirection REAL,"
      " precipitation REAL,"
      " radiation_direct REAL,"
      " radiation_direct_n REAL,"
      " radiation_diffuse REAL,"
      " radiation_shortwave REAL,"
      " radiation_tilted REAL,"
      " sun_duration REAL,"
      " PRIMARY KEY(facility_id, timestamp),"
      " FOREIGN KEY(facility_id) REFERENCES facility(id) ON DELETE CASCADE"
      ");"

      "CREATE INDEX IF NOT EXISTS idx_weather_values_facility_time "
      "ON weather_values(facility_id, timestamp);";

  pthread_mutex_lock(&_H->mutex);
  sqlite3_exec(_H->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
  char* err = NULL;

  if (sqlite3_exec(_H->db, sql, NULL, NULL, &err) != SQLITE_OK) {
    if (err) {
      fprintf(stderr, "sqlite error: %s\n", err);
      sqlite3_free(err);
    }
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }
  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}

static int sql_helper_get_or_create_facility(SqlHelper* _H, const Weather* _W, bool _forecast,
                                             sqlite3_int64* _facility_id)
{
  if (!_H || !_H->db || !_W || !_facility_id) {
    return ERR_INVALID_ARG;
  }

  const char* insert_sql =
      "INSERT INTO facility ("
      " latitude, longitude, panel_tilt, panel_azimuth, forecast, interval_minutes,"
      " temperature_unit, windspeed_unit, precipitation_unit, winddirection_unit, radiation_unit"
      " ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(latitude, longitude, panel_tilt, panel_azimuth, forecast) "
      "DO UPDATE SET "
      " interval_minutes = excluded.interval_minutes,"
      " temperature_unit = excluded.temperature_unit,"
      " windspeed_unit = excluded.windspeed_unit,"
      " precipitation_unit = excluded.precipitation_unit,"
      " winddirection_unit = excluded.winddirection_unit,"
      " radiation_unit = excluded.radiation_unit;";

  sqlite3_stmt* stmt = NULL;

  pthread_mutex_lock(&_H->mutex);
  if (sqlite3_prepare_v2(_H->db, insert_sql, -1, &stmt, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_H->db, "prepare facility insert failed");
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_bind_double(stmt, 1, _W->latitude);
  sqlite3_bind_double(stmt, 2, _W->longitude);
  sqlite3_bind_int(stmt, 3, _W->panel_tilt);
  sqlite3_bind_int(stmt, 4, _W->panel_azimuth);
  sqlite3_bind_int(stmt, 5, _forecast ? 1 : 0);
  sqlite3_bind_int(stmt, 6, _W->update_interval);

  sqlite3_bind_text(stmt, 7, _W->temperature_unit ? _W->temperature_unit : "", -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 8, _W->windspeed_unit ? _W->windspeed_unit : "", -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 9, _W->precipitation_unit ? _W->precipitation_unit : "", -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 10, _W->winddirection_unit ? _W->winddirection_unit : "", -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 11, _W->radiation_unit ? _W->radiation_unit : "", -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    SQL_CHECK_DB(_H->db, "step facility insert failed");
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_finalize(stmt);

  const char* select_sql = "SELECT id FROM facility "
                           "WHERE ABS(latitude - ?) < 0.0001 "
                           "AND ABS(longitude - ?) < 0.0001 "
                           "AND panel_tilt=? AND panel_azimuth=? AND forecast=?;";

  if (sqlite3_prepare_v2(_H->db, select_sql, -1, &stmt, NULL) != SQLITE_OK) {
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_bind_double(stmt, 1, _W->latitude);
  sqlite3_bind_double(stmt, 2, _W->longitude);
  sqlite3_bind_int(stmt, 3, _W->panel_tilt);
  sqlite3_bind_int(stmt, 4, _W->panel_azimuth);
  sqlite3_bind_int(stmt, 5, _forecast ? 1 : 0);

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_H->mutex);
    return ERR_NOT_FOUND;
  }

  *_facility_id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);

  pthread_mutex_unlock(&_H->mutex);

  return SUCCESS;
}

int sql_helper_insert_weather(SqlHelper* _H, const Weather* _W, bool _forecast)
{
  if (!_H || !_H->db || !_W || !_W->values || _W->count == 0) {
    return ERR_INVALID_ARG;
  }

  int res;
  sqlite3_int64 facility_id = 0;

  res = sql_helper_get_or_create_facility(_H, _W, _forecast, &facility_id);
  if (res != SUCCESS) {
    return res;
  }

  const char* sql = "INSERT INTO weather_values ("
                    " facility_id, timestamp, temperature, windspeed, winddirection, precipitation,"
                    " radiation_direct, radiation_direct_n, radiation_diffuse, radiation_shortwave,"
                    " radiation_tilted, sun_duration"
                    " ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(facility_id, timestamp) "
                    "DO UPDATE SET "
                    " temperature = excluded.temperature,"
                    " windspeed = excluded.windspeed,"
                    " winddirection = excluded.winddirection,"
                    " precipitation = excluded.precipitation,"
                    " radiation_direct = excluded.radiation_direct,"
                    " radiation_direct_n = excluded.radiation_direct_n,"
                    " radiation_diffuse = excluded.radiation_diffuse,"
                    " radiation_shortwave = excluded.radiation_shortwave,"
                    " radiation_tilted = excluded.radiation_tilted,"
                    " sun_duration = excluded.sun_duration;";

  sqlite3_stmt* stmt = NULL;

  pthread_mutex_lock(&_H->mutex);
  if (sqlite3_prepare_v2(_H->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_H->db, "prepare weather_values failed");
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_exec(_H->db, "BEGIN;", NULL, NULL, NULL);

  for (unsigned int i = 0; i < _W->count; i++) {
    const Weather_Values* v = &_W->values[i];

    sqlite3_bind_int64(stmt, 1, facility_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)v->timestamp);
    sqlite3_bind_double(stmt, 3, v->temperature);
    sqlite3_bind_double(stmt, 4, v->windspeed);
    sqlite3_bind_double(stmt, 5, v->winddirection_azimuth);
    sqlite3_bind_double(stmt, 6, v->precipitation);
    sqlite3_bind_double(stmt, 7, v->radiation_direct);
    sqlite3_bind_double(stmt, 8, v->radiation_direct_n);
    sqlite3_bind_double(stmt, 9, v->radiation_diffuse);
    sqlite3_bind_double(stmt, 10, v->radiation_shortwave);
    sqlite3_bind_double(stmt, 11, v->radiation_tilted);
    sqlite3_bind_double(stmt, 12, v->sun_duration);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      SQL_CHECK_DB(_H->db, "step weather_values failed");
      sqlite3_exec(_H->db, "ROLLBACK;", NULL, NULL, NULL);
      sqlite3_finalize(stmt);
      pthread_mutex_unlock(&_H->mutex);
      return ERR_FATAL;
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_exec(_H->db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_finalize(stmt);

  pthread_mutex_unlock(&_H->mutex);

  return SUCCESS;
}

int sql_helper_read_weather(SqlHelper* _H, Weather* _out, double _latitude, double _longitude,
                            int _panel_tilt, unsigned int _panel_azimuth, bool _forecast,
                            time_t _start, time_t _end)
{
  if (!_H || !_H->db || !_out) {
    return ERR_INVALID_ARG;
  }

  sqlite3_stmt* stmt = NULL;
  sqlite3_int64 facility_id = 0;

  const char* meta_sql = "SELECT id, interval_minutes, temperature_unit, windspeed_unit, "
                         "precipitation_unit, winddirection_unit, radiation_unit "
                         "FROM facility "
                         "WHERE ABS(latitude - ?) < 0.0001 "
                         "AND ABS(longitude - ?) < 0.0001 "
                         "AND panel_tilt=? AND panel_azimuth=? AND forecast=?;";

  pthread_mutex_lock(&_H->mutex);
  if (sqlite3_prepare_v2(_H->db, meta_sql, -1, &stmt, NULL) != SQLITE_OK) {
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_bind_double(stmt, 1, _latitude);
  sqlite3_bind_double(stmt, 2, _longitude);
  sqlite3_bind_int(stmt, 3, _panel_tilt);
  sqlite3_bind_int(stmt, 4, _panel_azimuth);
  sqlite3_bind_int(stmt, 5, _forecast ? 1 : 0);

  printf("read_weather meta lookup: lat=%f lon=%f tilt=%d az=%u forecast=%d\n", _latitude,
         _longitude, _panel_tilt, _panel_azimuth, _forecast ? 1 : 0);

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    if (sqlite3_step(stmt) != SQLITE_ROW) {
      printf("read_weather: no matching facility row\n");
      sqlite3_finalize(stmt);
      _out->count = 0;
      pthread_mutex_unlock(&_H->mutex);
      return SUCCESS;
    }
    printf("read_weather: facility_id=%lld\n", (long long)facility_id);
    sqlite3_finalize(stmt);
    _out->count = 0;
    pthread_mutex_unlock(&_H->mutex);

    return SUCCESS;
  }

  facility_id = sqlite3_column_int64(stmt, 0);
  _out->update_interval = sqlite3_column_int(stmt, 1);
  _out->latitude = _latitude;
  _out->longitude = _longitude;
  _out->panel_tilt = _panel_tilt;
  _out->panel_azimuth = _panel_azimuth;

  if (_out->temperature_unit)
    free((void*)_out->temperature_unit);
  if (_out->windspeed_unit)
    free((void*)_out->windspeed_unit);
  if (_out->precipitation_unit)
    free((void*)_out->precipitation_unit);
  if (_out->winddirection_unit)
    free((void*)_out->winddirection_unit);
  if (_out->radiation_unit)
    free((void*)_out->radiation_unit);

  _out->temperature_unit = strdup((const char*)sqlite3_column_text(stmt, 2));
  _out->windspeed_unit = strdup((const char*)sqlite3_column_text(stmt, 3));
  _out->precipitation_unit = strdup((const char*)sqlite3_column_text(stmt, 4));
  _out->winddirection_unit = strdup((const char*)sqlite3_column_text(stmt, 5));
  _out->radiation_unit = strdup((const char*)sqlite3_column_text(stmt, 6));

  sqlite3_finalize(stmt);

  const char* values_sql =
      "SELECT timestamp, temperature, windspeed, winddirection, precipitation, "
      "radiation_direct, radiation_direct_n, radiation_diffuse, radiation_shortwave, "
      "radiation_tilted, sun_duration "
      "FROM weather_values "
      "WHERE facility_id=? AND timestamp>=? AND timestamp<? "
      "ORDER BY timestamp;";

  if (sqlite3_prepare_v2(_H->db, values_sql, -1, &stmt, NULL) != SQLITE_OK) {
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_bind_int64(stmt, 1, facility_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)_start);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)_end);

  if (_out->values) {
    free(_out->values);
    _out->values = NULL;
  }

  _out->count = 0;
  int capacity = 128;

  printf("read_weather values lookup: facility_id=%lld start=%lld end=%lld\n",
         (long long)facility_id, (long long)_start, (long long)_end);

  _out->values = malloc(sizeof(Weather_Values) * capacity);
  if (!_out->values) {
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_H->mutex);
    return ERR_NO_MEMORY;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if ((int)_out->count >= capacity) {
      capacity *= 2;
      Weather_Values* tmp = realloc(_out->values, sizeof(Weather_Values) * capacity);
      if (!tmp) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&_H->mutex);
        return ERR_NO_MEMORY;
      }
      _out->values = tmp;
    }
    Weather_Values* v = &_out->values[_out->count++];
    memset(v, 0, sizeof(Weather_Values));

    v->timestamp = (time_t)sqlite3_column_int64(stmt, 0);
    v->temperature = sqlite3_column_double(stmt, 1);
    v->windspeed = sqlite3_column_double(stmt, 2);
    v->winddirection_azimuth = sqlite3_column_double(stmt, 3);
    v->precipitation = sqlite3_column_double(stmt, 4);
    v->radiation_direct = sqlite3_column_double(stmt, 5);
    v->radiation_direct_n = sqlite3_column_double(stmt, 6);
    v->radiation_diffuse = sqlite3_column_double(stmt, 7);
    v->radiation_shortwave = sqlite3_column_double(stmt, 8);
    v->radiation_tilted = sqlite3_column_double(stmt, 9);
    v->sun_duration = sqlite3_column_double(stmt, 10);
  }

  printf("read_weather: fetched %u rows\n", _out->count);
  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}

int sql_helper_insert_spots(SqlHelper* _H, const Electricity_Spots* _spot)
{
  if (!_H || !_H->db || !_spot || !_spot->prices) {
    return ERR_INVALID_ARG;
  }

  const char* sql = "INSERT INTO electricity_spots "
                    "(time_start, time_end, spot_price, price_class, currency) "
                    "VALUES (?, ?, ?, ?, ?) "
                    "ON CONFLICT(time_start, price_class, currency) "
                    "DO UPDATE SET "
                    " time_end = excluded.time_end,"
                    " spot_price = excluded.spot_price;";

  sqlite3_stmt* stmt = NULL;

  pthread_mutex_lock(&_H->mutex);
  if (sqlite3_prepare_v2(_H->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_exec(_H->db, "BEGIN;", NULL, NULL, NULL);

  for (int i = 0; i < (int)_spot->price_count; i++) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)_spot->prices[i].time_start);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)_spot->prices[i].time_end);
    sqlite3_bind_double(stmt, 3, _spot->prices[i].spot_price);
    sqlite3_bind_int(stmt, 4, (int)_spot->price_class);
    sqlite3_bind_int(stmt, 5, (int)_spot->currency);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      pthread_mutex_unlock(&_H->mutex);
      return ERR_FATAL;
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_exec(_H->db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_finalize(stmt);

  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}
int sql_helper_read_spots(SqlHelper* _H, Electricity_Spots* _out, SpotPriceClass _price_class,
                          SpotCurrency _currency, time_t _start, time_t _end)
{
  if (!_H || !_H->db || !_out) {
    return ERR_INVALID_ARG;
  }

  const char* sql = "SELECT time_start, time_end, spot_price "
                    "FROM electricity_spots "
                    "WHERE price_class=? AND currency=? "
                    "AND time_start>=? AND time_start<? "
                    "ORDER BY time_start;";

  sqlite3_stmt* stmt = NULL;

  pthread_mutex_lock(&_H->mutex);
  if (sqlite3_prepare_v2(_H->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_bind_int(stmt, 1, (int)_price_class);
  sqlite3_bind_int(stmt, 2, (int)_currency);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)_start);
  sqlite3_bind_int64(stmt, 4, (sqlite3_int64)_end);

  if (_out->prices) {
    free(_out->prices);
  }

  _out->price_count = 0;
  int capacity = 96;

  _out->prices = malloc(sizeof(Electricity_Spot_Price) * capacity);
  if (!_out->prices) {
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_H->mutex);
    return ERR_NO_MEMORY;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {

    if ((int)_out->price_count >= capacity) {
      capacity *= 2;
      Electricity_Spot_Price* tmp =
          realloc(_out->prices, sizeof(Electricity_Spot_Price) * capacity);

      if (!tmp) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&_H->mutex);
        return ERR_NO_MEMORY;
      }
      _out->prices = tmp;
    }

    Electricity_Spot_Price* p = &_out->prices[_out->price_count++];

    p->time_start = (time_t)sqlite3_column_int64(stmt, 0);
    p->time_end = (time_t)sqlite3_column_int64(stmt, 1);
    p->spot_price = sqlite3_column_double(stmt, 2);
  }

  _out->price_class = _price_class;
  _out->currency = _currency;

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}

void sql_helper_close(SqlHelper* _H)
{
  if (_H->db) {
    pthread_mutex_lock(&_H->mutex);
    sqlite3_close(_H->db);
    pthread_mutex_unlock(&_H->mutex);
  }
}

void sql_helper_dispose(SqlHelper* _H)
{
  if (!_H) {
    return;
  }
  sqlite3_close(_H->db);
  _H = NULL;
}
