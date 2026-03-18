#define _POSIX_C_SOURCE 200809L
#include "sqlite_helpers.h"
#include <maestroutils/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQL_CHECK_DB(_db, _msg) fprintf(stderr, "[SQL] %s: %s\n", _msg, sqlite3_errmsg(_db))

int sql_helper_open(sqlite3** _db, const char* _path)
{
  if (!_db || !_path) {
    return ERR_INVALID_ARG;
  }

  if (sqlite3_open(_path, _db) != SQLITE_OK) {
    return ERR_FATAL;
  }

  return SUCCESS;
}

int sql_helper_init_schema(sqlite3* _db)
{
  if (!_db) {
    return ERR_INVALID_ARG;
  }
  sqlite3_exec(_db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

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

      "CREATE INDEX IF NOT EXISTS idx_spots_series_time "
      "ON electricity_spots(price_class, currency, time_start);"

      /* Weather meta/config */
      "CREATE TABLE IF NOT EXISTS weather_series ("
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
      " series_id INTEGER NOT NULL,"
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
      " PRIMARY KEY(series_id, timestamp),"
      " FOREIGN KEY(series_id) REFERENCES weather_series(id) ON DELETE CASCADE"
      ");"

      "CREATE INDEX IF NOT EXISTS idx_weather_values_series_time "
      "ON weather_values(series_id, timestamp);";

  char* err = NULL;

  if (sqlite3_exec(_db, sql, NULL, NULL, &err) != SQLITE_OK) {
    if (err) {
      fprintf(stderr, "sqlite error: %s\n", err);
      sqlite3_free(err);
    }
    return ERR_FATAL;
  }

  return SUCCESS;
}
static int sql_helper_get_or_create_weather_series(sqlite3* _db, const Weather* _W, bool _forecast,
                                                   sqlite3_int64* _series_id)
{
  if (!_db || !_W || !_series_id) {
    return ERR_INVALID_ARG;
  }

  const char* insert_sql =
      "INSERT INTO weather_series ("
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
  if (sqlite3_prepare_v2(_db, insert_sql, -1, &stmt, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_db, "prepare weather_series insert failed");
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
    SQL_CHECK_DB(_db, "step weather_series insert failed");
    sqlite3_finalize(stmt);
    return ERR_FATAL;
  }

  sqlite3_finalize(stmt);

  const char* select_sql =
      "SELECT id FROM weather_series "
      "WHERE latitude=? AND longitude=? AND panel_tilt=? AND panel_azimuth=? AND forecast=?;";

  if (sqlite3_prepare_v2(_db, select_sql, -1, &stmt, NULL) != SQLITE_OK) {
    return ERR_FATAL;
  }

  sqlite3_bind_double(stmt, 1, _W->latitude);
  sqlite3_bind_double(stmt, 2, _W->longitude);
  sqlite3_bind_int(stmt, 3, _W->panel_tilt);
  sqlite3_bind_int(stmt, 4, _W->panel_azimuth);
  sqlite3_bind_int(stmt, 5, _forecast ? 1 : 0);

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return ERR_NOT_FOUND;
  }

  *_series_id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);

  return SUCCESS;
}

int sql_helper_insert_weather(sqlite3* _db, const Weather* _W, bool _forecast)
{
  if (!_db || !_W || !_W->values || _W->count == 0) {
    return ERR_INVALID_ARG;
  }

  int res;
  sqlite3_int64 series_id = 0;

  res = sql_helper_get_or_create_weather_series(_db, _W, _forecast, &series_id);
  if (res != SUCCESS) {
    return res;
  }

  const char* sql = "INSERT INTO weather_values ("
                    " series_id, timestamp, temperature, windspeed, winddirection, precipitation,"
                    " radiation_direct, radiation_direct_n, radiation_diffuse, radiation_shortwave,"
                    " radiation_tilted, sun_duration"
                    " ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(series_id, timestamp) "
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
  if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_db, "prepare weather_values failed");
    return ERR_FATAL;
  }

  sqlite3_exec(_db, "BEGIN;", NULL, NULL, NULL);

  for (unsigned int i = 0; i < _W->count; i++) {
    const Weather_Values* v = &_W->values[i];

    sqlite3_bind_int64(stmt, 1, series_id);
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
      SQL_CHECK_DB(_db, "step weather_values failed");
      sqlite3_exec(_db, "ROLLBACK;", NULL, NULL, NULL);
      sqlite3_finalize(stmt);
      return ERR_FATAL;
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_exec(_db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_finalize(stmt);

  return SUCCESS;
}

int sql_helper_read_weather(sqlite3* _db, Weather* _out, double _latitude, double _longitude,
                            int _panel_tilt, unsigned int _panel_azimuth, bool _forecast,
                            time_t _start, time_t _end)
{
  if (!_db || !_out) {
    return ERR_INVALID_ARG;
  }

  sqlite3_stmt* stmt = NULL;
  sqlite3_int64 series_id = 0;

  const char* meta_sql =
      "SELECT id, interval_minutes, temperature_unit, windspeed_unit, "
      "precipitation_unit, winddirection_unit, radiation_unit "
      "FROM weather_series "
      "WHERE latitude=? AND longitude=? AND panel_tilt=? AND panel_azimuth=? AND forecast=?;";

  if (sqlite3_prepare_v2(_db, meta_sql, -1, &stmt, NULL) != SQLITE_OK) {
    return ERR_FATAL;
  }

  sqlite3_bind_double(stmt, 1, _latitude);
  sqlite3_bind_double(stmt, 2, _longitude);
  sqlite3_bind_int(stmt, 3, _panel_tilt);
  sqlite3_bind_int(stmt, 4, _panel_azimuth);
  sqlite3_bind_int(stmt, 5, _forecast ? 1 : 0);

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    _out->count = 0;
    return SUCCESS;
  }

  series_id = sqlite3_column_int64(stmt, 0);
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
      "WHERE series_id=? AND timestamp>=? AND timestamp<? "
      "ORDER BY timestamp;";

  if (sqlite3_prepare_v2(_db, values_sql, -1, &stmt, NULL) != SQLITE_OK) {
    return ERR_FATAL;
  }

  sqlite3_bind_int64(stmt, 1, series_id);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)_start);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)_end);

  if (_out->values) {
    free(_out->values);
    _out->values = NULL;
  }

  _out->count = 0;
  int capacity = 128;

  _out->values = malloc(sizeof(Weather_Values) * capacity);
  if (!_out->values) {
    sqlite3_finalize(stmt);
    return ERR_NO_MEMORY;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if ((int)_out->count >= capacity) {
      capacity *= 2;
      Weather_Values* tmp = realloc(_out->values, sizeof(Weather_Values) * capacity);
      if (!tmp) {
        sqlite3_finalize(stmt);
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

  sqlite3_finalize(stmt);
  return SUCCESS;
}

int sql_helper_insert_spots(sqlite3* _db, const Electricity_Spots* _spot)
{
  if (!_db || !_spot || !_spot->prices) {
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

  if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return ERR_FATAL;
  }

  sqlite3_exec(_db, "BEGIN;", NULL, NULL, NULL);

  for (int i = 0; i < (int)_spot->price_count; i++) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)_spot->prices[i].time_start);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)_spot->prices[i].time_end);
    sqlite3_bind_double(stmt, 3, _spot->prices[i].spot_price);
    sqlite3_bind_int(stmt, 4, (int)_spot->price_class);
    sqlite3_bind_int(stmt, 5, (int)_spot->currency);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return ERR_FATAL;
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_exec(_db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_finalize(stmt);

  return SUCCESS;
}
int sql_helper_read_spots(sqlite3* _db, Electricity_Spots* _out, SpotPriceClass _price_class,
                          SpotCurrency _currency, time_t _start, time_t _end)
{
  if (!_db || !_out) {
    return ERR_INVALID_ARG;
  }

  const char* sql = "SELECT time_start, time_end, spot_price "
                    "FROM electricity_spots "
                    "WHERE price_class=? AND currency=? "
                    "AND time_start>=? AND time_start<? "
                    "ORDER BY time_start;";

  sqlite3_stmt* stmt = NULL;

  if (sqlite3_prepare_v2(_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
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
    return ERR_NO_MEMORY;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {

    if ((int)_out->price_count >= capacity) {
      capacity *= 2;
      Electricity_Spot_Price* tmp =
          realloc(_out->prices, sizeof(Electricity_Spot_Price) * capacity);

      if (!tmp) {
        sqlite3_finalize(stmt);
        return ERR_NO_MEMORY;
      }
      _out->prices = tmp;
    }

    Electricity_Spot_Price* p = &_out->prices[_out->price_count++];

    printf("Pricecount in sqlhelper: %d\n", _out->price_count);

    p->time_start = (time_t)sqlite3_column_int64(stmt, 0);
    p->time_end = (time_t)sqlite3_column_int64(stmt, 1);
    p->spot_price = sqlite3_column_double(stmt, 2);
  }

  _out->price_class = _price_class;
  _out->currency = _currency;

  sqlite3_finalize(stmt);

  return SUCCESS;
}

void sql_helper_close(sqlite3* _db)
{
  if (_db) {
    sqlite3_close(_db);
  }
}
