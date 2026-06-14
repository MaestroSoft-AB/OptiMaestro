#define _POSIX_C_SOURCE 200809L
#include "sqlite_helpers.h"
#include <maestroutils/error.h>
#include <maestroutils/file_logging.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQL_CHECK_DB(_db, _msg) fprintf(stderr, "[SQL] %s: %s\n", _msg, sqlite3_errmsg(_db))
#define SQL_HELPER_CACHE_RETENTION_DAYS 30

static int sql_helper_prune_cache_locked(SqlHelper* _H, time_t _cutoff);

int sql_helper_init(SqlHelper* _H) {
  if (!_H) {
    return ERR_INVALID_ARG;
  }

  _H->db = NULL;

  if (pthread_mutex_init(&_H->mutex, NULL) != 0) {
    return ERR_FATAL;
  }

  return SUCCESS;
}

int sql_helper_open(SqlHelper* _H, const char* _path) {
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

int sql_helper_init_schema(SqlHelper* _H) {
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
      "ON weather_values(facility_id, timestamp);"

      /* HomeWizard P1 meter readings */
      "CREATE TABLE IF NOT EXISTS meter_readings ("
      " id INTEGER PRIMARY KEY AUTOINCREMENT,"
      " received_at INTEGER NOT NULL DEFAULT (unixepoch()),"
      " present_flags INTEGER NOT NULL,"
      " unique_id TEXT,"
      " meter_model TEXT,"
      " timestamp TEXT,"
      " protocol_version INTEGER,"
      " tariff INTEGER,"
      " energy_import_kwh REAL,"
      " energy_import_t1_kwh REAL,"
      " energy_import_t2_kwh REAL,"
      " energy_import_t3_kwh REAL,"
      " energy_import_t4_kwh REAL,"
      " energy_export_kwh REAL,"
      " energy_export_t1_kwh REAL,"
      " energy_export_t2_kwh REAL,"
      " energy_export_t3_kwh REAL,"
      " energy_export_t4_kwh REAL,"
      " power_w REAL,"
      " power_l1_w REAL,"
      " power_l2_w REAL,"
      " power_l3_w REAL,"
      " voltage_v REAL,"
      " voltage_l1_v REAL,"
      " voltage_l2_v REAL,"
      " voltage_l3_v REAL,"
      " current_a REAL,"
      " current_l1_a REAL,"
      " current_l2_a REAL,"
      " current_l3_a REAL,"
      " frequency_hz REAL,"
      " voltage_sag_l1_count INTEGER,"
      " voltage_sag_l2_count INTEGER,"
      " voltage_sag_l3_count INTEGER,"
      " voltage_swell_l1_count INTEGER,"
      " voltage_swell_l2_count INTEGER,"
      " voltage_swell_l3_count INTEGER,"
      " any_power_fail_count INTEGER,"
      " long_power_fail_count INTEGER,"
      " average_power_15m_w REAL,"
      " monthly_power_peak_w REAL,"
      " monthly_power_peak_timestamp TEXT"
      ");"

      "CREATE INDEX IF NOT EXISTS idx_meter_readings_received_at "
      "ON meter_readings(received_at);"

      "CREATE INDEX IF NOT EXISTS idx_meter_readings_timestamp "
      "ON meter_readings(timestamp);";

  pthread_mutex_lock(&_H->mutex);
  sqlite3_exec(_H->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
  char* err = NULL;

  if (sqlite3_exec(_H->db, sql, NULL, NULL, &err) != SQLITE_OK) {
    if (err) {
      LOG_ERROR("sqlite error: %s\n", err);
      sqlite3_free(err);
    }
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }
  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}

static int sql_helper_get_or_create_facility(SqlHelper* _H, const Weather* _W, bool _forecast,
                                             sqlite3_int64* _facility_id) {
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

int sql_helper_insert_weather(SqlHelper* _H, const Weather* _W, bool _forecast) {
  if (!_H || !_H->db || !_W || !_W->values || _W->count == 0) {
    return ERR_INVALID_ARG;
  }

  int           res;
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

  int prune_res = sql_helper_prune_cache_locked(_H, time(NULL) - (SQL_HELPER_CACHE_RETENTION_DAYS * 86400));
  if (prune_res != SUCCESS) {
    pthread_mutex_unlock(&_H->mutex);
    return prune_res;
  }

  pthread_mutex_unlock(&_H->mutex);

  return SUCCESS;
}

int sql_helper_read_weather(SqlHelper* _H, Weather* _out, double _latitude, double _longitude,
                            int _panel_tilt, unsigned int _panel_azimuth, bool _forecast,
                            time_t _start, time_t _end) {
  if (!_H || !_H->db || !_out) {
    return ERR_INVALID_ARG;
  }

  sqlite3_stmt* stmt        = NULL;
  sqlite3_int64 facility_id = 0;

  const char* meta_sql = "SELECT id, interval_minutes, temperature_unit, windspeed_unit, "
                         "precipitation_unit, winddirection_unit, radiation_unit, "
                         "panel_tilt, panel_azimuth "
                         "FROM facility "
                         "WHERE ABS(latitude - ?) < 0.0001 "
                         "AND ABS(longitude - ?) < 0.0001 "
                         "AND forecast=? "
                         "ORDER BY CASE WHEN panel_tilt=? AND panel_azimuth=? THEN 0 ELSE 1 END "
                         "LIMIT 1;";

  pthread_mutex_lock(&_H->mutex);
  if (sqlite3_prepare_v2(_H->db, meta_sql, -1, &stmt, NULL) != SQLITE_OK) {
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_bind_double(stmt, 1, _latitude);
  sqlite3_bind_double(stmt, 2, _longitude);
  sqlite3_bind_int(stmt, 3, _forecast ? 1 : 0);
  sqlite3_bind_int(stmt, 4, _panel_tilt);
  sqlite3_bind_int(stmt, 5, _panel_azimuth);

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    _out->count = 0;
    pthread_mutex_unlock(&_H->mutex);
    return SUCCESS;
  }

  facility_id           = sqlite3_column_int64(stmt, 0);
  _out->update_interval = sqlite3_column_int(stmt, 1);
  _out->latitude        = _latitude;
  _out->longitude       = _longitude;
  _out->panel_tilt      = sqlite3_column_int(stmt, 7);
  _out->panel_azimuth   = sqlite3_column_int(stmt, 8);

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

  _out->temperature_unit   = strdup((const char*)sqlite3_column_text(stmt, 2));
  _out->windspeed_unit     = strdup((const char*)sqlite3_column_text(stmt, 3));
  _out->precipitation_unit = strdup((const char*)sqlite3_column_text(stmt, 4));
  _out->winddirection_unit = strdup((const char*)sqlite3_column_text(stmt, 5));
  _out->radiation_unit     = strdup((const char*)sqlite3_column_text(stmt, 6));

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

  _out->count  = 0;
  int capacity = 128;

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

    v->timestamp             = (time_t)sqlite3_column_int64(stmt, 0);
    v->temperature           = sqlite3_column_double(stmt, 1);
    v->windspeed             = sqlite3_column_double(stmt, 2);
    v->winddirection_azimuth = sqlite3_column_double(stmt, 3);
    v->precipitation         = sqlite3_column_double(stmt, 4);
    v->radiation_direct      = sqlite3_column_double(stmt, 5);
    v->radiation_direct_n    = sqlite3_column_double(stmt, 6);
    v->radiation_diffuse     = sqlite3_column_double(stmt, 7);
    v->radiation_shortwave   = sqlite3_column_double(stmt, 8);
    v->radiation_tilted      = sqlite3_column_double(stmt, 9);
    v->sun_duration          = sqlite3_column_double(stmt, 10);
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}

int sql_helper_insert_spots(SqlHelper* _H, const Electricity_Spots* _spot) {
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

  int prune_res = sql_helper_prune_cache_locked(_H, time(NULL) - (SQL_HELPER_CACHE_RETENTION_DAYS * 86400));
  if (prune_res != SUCCESS) {
    pthread_mutex_unlock(&_H->mutex);
    return prune_res;
  }

  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}
int sql_helper_read_spots(SqlHelper* _H, Electricity_Spots* _out, SpotPriceClass _price_class,
                          SpotCurrency _currency, time_t _start, time_t _end) {
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
  int capacity      = 96;

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
    p->time_end   = (time_t)sqlite3_column_int64(stmt, 1);
    p->spot_price = sqlite3_column_double(stmt, 2);
  }

  _out->price_class = _price_class;
  _out->currency    = _currency;

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&_H->mutex);
  return SUCCESS;
}

static void sql_helper_copy_column_text(sqlite3_stmt* _stmt, int _col, char* _dest, size_t _size) {
  const unsigned char* text;

  if (!_dest || _size == 0) {
    return;
  }

  _dest[0] = '\0';
  text     = sqlite3_column_text(_stmt, _col);
  if (!text) {
    return;
  }

  snprintf(_dest, _size, "%s", (const char*)text);
}

int sql_helper_insert_meter_reading(SqlHelper* _H, const Meter_Reading* _reading) {
  if (!_H || !_H->db || !_reading) {
    return ERR_INVALID_ARG;
  }

  const char* sql =
      "INSERT INTO meter_readings ("
      " present_flags, unique_id, meter_model, timestamp, protocol_version, tariff,"
      " energy_import_kwh, energy_import_t1_kwh, energy_import_t2_kwh, energy_import_t3_kwh,"
      " energy_import_t4_kwh, energy_export_kwh, energy_export_t1_kwh, energy_export_t2_kwh,"
      " energy_export_t3_kwh, energy_export_t4_kwh, power_w, power_l1_w, power_l2_w, power_l3_w,"
      " voltage_v, voltage_l1_v, voltage_l2_v, voltage_l3_v, current_a, current_l1_a,"
      " current_l2_a, current_l3_a, frequency_hz, voltage_sag_l1_count, voltage_sag_l2_count,"
      " voltage_sag_l3_count, voltage_swell_l1_count, voltage_swell_l2_count,"
      " voltage_swell_l3_count, any_power_fail_count, long_power_fail_count,"
      " average_power_15m_w, monthly_power_peak_w, monthly_power_peak_timestamp"
      " ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
      "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

  sqlite3_stmt* stmt = NULL;

  pthread_mutex_lock(&_H->mutex);

  if (sqlite3_prepare_v2(_H->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_H->db, "prepare meter_readings insert failed");
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_exec(_H->db, "BEGIN;", NULL, NULL, NULL);

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)_reading->present_flags);
  sqlite3_bind_text(stmt, 2, _reading->unique_id, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, _reading->meter_model, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, _reading->timestamp, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 5, _reading->protocol_version);
  sqlite3_bind_int(stmt, 6, _reading->tariff);
  sqlite3_bind_double(stmt, 7, _reading->energy_import_kwh);
  sqlite3_bind_double(stmt, 8, _reading->energy_import_t1_kwh);
  sqlite3_bind_double(stmt, 9, _reading->energy_import_t2_kwh);
  sqlite3_bind_double(stmt, 10, _reading->energy_import_t3_kwh);
  sqlite3_bind_double(stmt, 11, _reading->energy_import_t4_kwh);
  sqlite3_bind_double(stmt, 12, _reading->energy_export_kwh);
  sqlite3_bind_double(stmt, 13, _reading->energy_export_t1_kwh);
  sqlite3_bind_double(stmt, 14, _reading->energy_export_t2_kwh);
  sqlite3_bind_double(stmt, 15, _reading->energy_export_t3_kwh);
  sqlite3_bind_double(stmt, 16, _reading->energy_export_t4_kwh);
  sqlite3_bind_double(stmt, 17, _reading->power_w);
  sqlite3_bind_double(stmt, 18, _reading->power_l1_w);
  sqlite3_bind_double(stmt, 19, _reading->power_l2_w);
  sqlite3_bind_double(stmt, 20, _reading->power_l3_w);
  sqlite3_bind_double(stmt, 21, _reading->voltage_v);
  sqlite3_bind_double(stmt, 22, _reading->voltage_l1_v);
  sqlite3_bind_double(stmt, 23, _reading->voltage_l2_v);
  sqlite3_bind_double(stmt, 24, _reading->voltage_l3_v);
  sqlite3_bind_double(stmt, 25, _reading->current_a);
  sqlite3_bind_double(stmt, 26, _reading->current_l1_a);
  sqlite3_bind_double(stmt, 27, _reading->current_l2_a);
  sqlite3_bind_double(stmt, 28, _reading->current_l3_a);
  sqlite3_bind_double(stmt, 29, _reading->frequency_hz);
  sqlite3_bind_int64(stmt, 30, _reading->voltage_sag_l1_count);
  sqlite3_bind_int64(stmt, 31, _reading->voltage_sag_l2_count);
  sqlite3_bind_int64(stmt, 32, _reading->voltage_sag_l3_count);
  sqlite3_bind_int64(stmt, 33, _reading->voltage_swell_l1_count);
  sqlite3_bind_int64(stmt, 34, _reading->voltage_swell_l2_count);
  sqlite3_bind_int64(stmt, 35, _reading->voltage_swell_l3_count);
  sqlite3_bind_int64(stmt, 36, _reading->any_power_fail_count);
  sqlite3_bind_int64(stmt, 37, _reading->long_power_fail_count);
  sqlite3_bind_double(stmt, 38, _reading->average_power_15m_w);
  sqlite3_bind_double(stmt, 39, _reading->monthly_power_peak_w);
  sqlite3_bind_text(stmt, 40, _reading->monthly_power_peak_timestamp, -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    SQL_CHECK_DB(_H->db, "step meter_readings insert failed");
    sqlite3_exec(_H->db, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  sqlite3_finalize(stmt);

  sqlite3_exec(_H->db, "COMMIT;", NULL, NULL, NULL);
  pthread_mutex_unlock(&_H->mutex);

  return SUCCESS;
}

int sql_helper_read_latest_meter_reading(SqlHelper* _H, Meter_Reading* _out) {
  if (!_H || !_H->db || !_out) {
    return ERR_INVALID_ARG;
  }

  const char* sql =
      "SELECT id, present_flags, unique_id, meter_model, timestamp, protocol_version, tariff,"
      " energy_import_kwh, energy_import_t1_kwh, energy_import_t2_kwh, energy_import_t3_kwh,"
      " energy_import_t4_kwh, energy_export_kwh, energy_export_t1_kwh, energy_export_t2_kwh,"
      " energy_export_t3_kwh, energy_export_t4_kwh, power_w, power_l1_w, power_l2_w, power_l3_w,"
      " voltage_v, voltage_l1_v, voltage_l2_v, voltage_l3_v, current_a, current_l1_a,"
      " current_l2_a, current_l3_a, frequency_hz, voltage_sag_l1_count, voltage_sag_l2_count,"
      " voltage_sag_l3_count, voltage_swell_l1_count, voltage_swell_l2_count,"
      " voltage_swell_l3_count, any_power_fail_count, long_power_fail_count,"
      " average_power_15m_w, monthly_power_peak_w, monthly_power_peak_timestamp "
      "FROM meter_readings "
      "ORDER BY id DESC "
      "LIMIT 1;";

  sqlite3_stmt* stmt = NULL;

  memset(_out, 0, sizeof(Meter_Reading));

  pthread_mutex_lock(&_H->mutex);

  if (sqlite3_prepare_v2(_H->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_H->db, "prepare latest meter_readings select failed");
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  int step_res = sqlite3_step(stmt);
  if (step_res == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_H->mutex);
    return ERR_NOT_FOUND;
  }

  if (step_res != SQLITE_ROW) {
    SQL_CHECK_DB(_H->db, "step latest meter_readings select failed");
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_H->mutex);
    return ERR_FATAL;
  }

  _out->present_flags = (uint64_t)sqlite3_column_int64(stmt, 1);
  sql_helper_copy_column_text(stmt, 2, _out->unique_id, sizeof(_out->unique_id));
  sql_helper_copy_column_text(stmt, 3, _out->meter_model, sizeof(_out->meter_model));
  sql_helper_copy_column_text(stmt, 4, _out->timestamp, sizeof(_out->timestamp));
  _out->protocol_version       = (uint16_t)sqlite3_column_int(stmt, 5);
  _out->tariff                 = (uint8_t)sqlite3_column_int(stmt, 6);
  _out->energy_import_kwh      = sqlite3_column_double(stmt, 7);
  _out->energy_import_t1_kwh   = sqlite3_column_double(stmt, 8);
  _out->energy_import_t2_kwh   = sqlite3_column_double(stmt, 9);
  _out->energy_import_t3_kwh   = sqlite3_column_double(stmt, 10);
  _out->energy_import_t4_kwh   = sqlite3_column_double(stmt, 11);
  _out->energy_export_kwh      = sqlite3_column_double(stmt, 12);
  _out->energy_export_t1_kwh   = sqlite3_column_double(stmt, 13);
  _out->energy_export_t2_kwh   = sqlite3_column_double(stmt, 14);
  _out->energy_export_t3_kwh   = sqlite3_column_double(stmt, 15);
  _out->energy_export_t4_kwh   = sqlite3_column_double(stmt, 16);
  _out->power_w                = sqlite3_column_double(stmt, 17);
  _out->power_l1_w             = sqlite3_column_double(stmt, 18);
  _out->power_l2_w             = sqlite3_column_double(stmt, 19);
  _out->power_l3_w             = sqlite3_column_double(stmt, 20);
  _out->voltage_v              = sqlite3_column_double(stmt, 21);
  _out->voltage_l1_v           = sqlite3_column_double(stmt, 22);
  _out->voltage_l2_v           = sqlite3_column_double(stmt, 23);
  _out->voltage_l3_v           = sqlite3_column_double(stmt, 24);
  _out->current_a              = sqlite3_column_double(stmt, 25);
  _out->current_l1_a           = sqlite3_column_double(stmt, 26);
  _out->current_l2_a           = sqlite3_column_double(stmt, 27);
  _out->current_l3_a           = sqlite3_column_double(stmt, 28);
  _out->frequency_hz           = sqlite3_column_double(stmt, 29);
  _out->voltage_sag_l1_count   = (uint32_t)sqlite3_column_int64(stmt, 30);
  _out->voltage_sag_l2_count   = (uint32_t)sqlite3_column_int64(stmt, 31);
  _out->voltage_sag_l3_count   = (uint32_t)sqlite3_column_int64(stmt, 32);
  _out->voltage_swell_l1_count = (uint32_t)sqlite3_column_int64(stmt, 33);
  _out->voltage_swell_l2_count = (uint32_t)sqlite3_column_int64(stmt, 34);
  _out->voltage_swell_l3_count = (uint32_t)sqlite3_column_int64(stmt, 35);
  _out->any_power_fail_count   = (uint32_t)sqlite3_column_int64(stmt, 36);
  _out->long_power_fail_count  = (uint32_t)sqlite3_column_int64(stmt, 37);
  _out->average_power_15m_w    = sqlite3_column_double(stmt, 38);
  _out->monthly_power_peak_w   = sqlite3_column_double(stmt, 39);
  sql_helper_copy_column_text(stmt, 40, _out->monthly_power_peak_timestamp,
                              sizeof(_out->monthly_power_peak_timestamp));

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&_H->mutex);

  return SUCCESS;
}

static int sql_helper_prune_cache_locked(SqlHelper* _H, time_t _cutoff) {
  if (!_H || !_H->db) {
    return ERR_INVALID_ARG;
  }

  const char* sql = "DELETE FROM electricity_spots WHERE time_start < ?;"
                    "DELETE FROM weather_values WHERE timestamp < ?;"
                    "DELETE FROM facility WHERE id NOT IN "
                    "(SELECT DISTINCT facility_id FROM weather_values);";

  sqlite3_stmt* stmt = NULL;
  const char* tail = sql;
  while (tail && *tail != '\0') {
    if (sqlite3_prepare_v2(_H->db, tail, -1, &stmt, &tail) != SQLITE_OK) {
      SQL_CHECK_DB(_H->db, "prepare cache prune failed");
      return ERR_FATAL;
    }
    if (!stmt) {
      continue;
    }
    if (sqlite3_bind_parameter_count(stmt) == 1) {
      sqlite3_bind_int64(stmt, 1, (sqlite3_int64)_cutoff);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      SQL_CHECK_DB(_H->db, "step cache prune failed");
      sqlite3_finalize(stmt);
      return ERR_FATAL;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
  }

  return SUCCESS;
}

void sql_helper_close(SqlHelper* _H) {
  if (_H->db) {
    pthread_mutex_lock(&_H->mutex);
    sqlite3_close(_H->db);
    _H->db = NULL;
    pthread_mutex_unlock(&_H->mutex);
  }
}

void sql_helper_dispose(SqlHelper* _H) {
  sql_helper_close(_H);

  if (!_H) {
    return;
  }
  _H = NULL;
}
