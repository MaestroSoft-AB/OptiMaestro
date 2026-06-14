#define _POSIX_C_SOURCE 200809L
#include "meter_reading_store.h"
#include <maestroutils/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQL_CHECK_DB(_db, _msg) fprintf(stderr, "[SQL] %s: %s\n", _msg, sqlite3_errmsg(_db))
#define METER_STORE_BUSY_TIMEOUT_MS 5000

static void meter_store_copy_column_text(sqlite3_stmt* _stmt, int _col, char* _dest,
                                         size_t _size);
static void meter_store_bind_text_if_present(sqlite3_stmt* _stmt, int _idx, uint64_t _flags,
                                             uint64_t _present_flag, const char* _value);
static void meter_store_bind_int_if_present(sqlite3_stmt* _stmt, int _idx, uint64_t _flags,
                                            uint64_t _present_flag, int _value);
static void meter_store_bind_int64_if_present(sqlite3_stmt* _stmt, int _idx, uint64_t _flags,
                                              uint64_t _present_flag, sqlite3_int64 _value);
static void meter_store_bind_double_if_present(sqlite3_stmt* _stmt, int _idx, uint64_t _flags,
                                               uint64_t _present_flag, double _value);
static void meter_store_read_stmt_row(sqlite3_stmt* _stmt, Meter_Reading_Row* _row);

int meter_store_init(Meter_Reading_Store* _S) {
  if (!_S) {
    return ERR_INVALID_ARG;
  }

  _S->db = NULL;

  if (pthread_mutex_init(&_S->mutex, NULL) != 0) {
    return ERR_FATAL;
  }

  return SUCCESS;
}

int meter_store_open(Meter_Reading_Store* _S, const char* _path, bool _readonly) {
  if (!_S || !_path) {
    return ERR_INVALID_ARG;
  }

  int flags = _readonly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);

  pthread_mutex_lock(&_S->mutex);

  if (sqlite3_open_v2(_path, &_S->db, flags, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_S->db, "meter_store_open failed");
    pthread_mutex_unlock(&_S->mutex);
    return ERR_FATAL;
  }

  sqlite3_busy_timeout(_S->db, METER_STORE_BUSY_TIMEOUT_MS);

  if (!_readonly) {
    char* err = NULL;
    if (sqlite3_exec(_S->db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err) != SQLITE_OK) {
      if (err) {
        fprintf(stderr, "[SQL] meter_store_open WAL failed: %s\n", err);
        sqlite3_free(err);
      }
      sqlite3_close(_S->db);
      _S->db = NULL;
      pthread_mutex_unlock(&_S->mutex);
      return ERR_FATAL;
    }
  }

  pthread_mutex_unlock(&_S->mutex);
  return SUCCESS;
}

int meter_store_init_schema(Meter_Reading_Store* _S) {
  if (!_S || !_S->db) {
    return ERR_INVALID_ARG;
  }

  const char* sql =
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

  pthread_mutex_lock(&_S->mutex);

  char* err = NULL;
  if (sqlite3_exec(_S->db, sql, NULL, NULL, &err) != SQLITE_OK) {
    if (err) {
      fprintf(stderr, "[SQL] meter_store_init_schema failed: %s\n", err);
      sqlite3_free(err);
    }
    pthread_mutex_unlock(&_S->mutex);
    return ERR_FATAL;
  }

  pthread_mutex_unlock(&_S->mutex);
  return SUCCESS;
}

int meter_store_insert_reading(Meter_Reading_Store* _S, const Meter_Reading* _reading) {
  if (!_S || !_S->db || !_reading) {
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

  pthread_mutex_lock(&_S->mutex);

  if (sqlite3_prepare_v2(_S->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_S->db, "prepare meter_readings insert failed");
    pthread_mutex_unlock(&_S->mutex);
    return ERR_FATAL;
  }

  sqlite3_exec(_S->db, "BEGIN;", NULL, NULL, NULL);

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)_reading->present_flags);
  meter_store_bind_text_if_present(stmt, 2, _reading->present_flags, METER_READING_PRESENT_ID,
                                   _reading->unique_id);
  meter_store_bind_text_if_present(stmt, 3, _reading->present_flags, METER_READING_PRESENT_MODEL,
                                   _reading->meter_model);
  meter_store_bind_text_if_present(stmt, 4, _reading->present_flags, METER_READING_PRESENT_TIMESTAMP,
                                   _reading->timestamp);
  meter_store_bind_int_if_present(stmt, 5, _reading->present_flags,
                                  METER_READING_PRESENT_PROTOCOL_VERSION,
                                  _reading->protocol_version);
  meter_store_bind_int_if_present(stmt, 6, _reading->present_flags, METER_READING_PRESENT_TARIFF,
                                  _reading->tariff);
  meter_store_bind_double_if_present(stmt, 7, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_IMPORT,
                                     _reading->energy_import_kwh);
  meter_store_bind_double_if_present(stmt, 8, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_IMPORT_T1,
                                     _reading->energy_import_t1_kwh);
  meter_store_bind_double_if_present(stmt, 9, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_IMPORT_T2,
                                     _reading->energy_import_t2_kwh);
  meter_store_bind_double_if_present(stmt, 10, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_IMPORT_T3,
                                     _reading->energy_import_t3_kwh);
  meter_store_bind_double_if_present(stmt, 11, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_IMPORT_T4,
                                     _reading->energy_import_t4_kwh);
  meter_store_bind_double_if_present(stmt, 12, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_EXPORT,
                                     _reading->energy_export_kwh);
  meter_store_bind_double_if_present(stmt, 13, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_EXPORT_T1,
                                     _reading->energy_export_t1_kwh);
  meter_store_bind_double_if_present(stmt, 14, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_EXPORT_T2,
                                     _reading->energy_export_t2_kwh);
  meter_store_bind_double_if_present(stmt, 15, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_EXPORT_T3,
                                     _reading->energy_export_t3_kwh);
  meter_store_bind_double_if_present(stmt, 16, _reading->present_flags,
                                     METER_READING_PRESENT_ENERGY_EXPORT_T4,
                                     _reading->energy_export_t4_kwh);
  meter_store_bind_double_if_present(stmt, 17, _reading->present_flags,
                                     METER_READING_PRESENT_POWER, _reading->power_w);
  meter_store_bind_double_if_present(stmt, 18, _reading->present_flags,
                                     METER_READING_PRESENT_POWER_L1, _reading->power_l1_w);
  meter_store_bind_double_if_present(stmt, 19, _reading->present_flags,
                                     METER_READING_PRESENT_POWER_L2, _reading->power_l2_w);
  meter_store_bind_double_if_present(stmt, 20, _reading->present_flags,
                                     METER_READING_PRESENT_POWER_L3, _reading->power_l3_w);
  meter_store_bind_double_if_present(stmt, 21, _reading->present_flags,
                                     METER_READING_PRESENT_VOLTAGE, _reading->voltage_v);
  meter_store_bind_double_if_present(stmt, 22, _reading->present_flags,
                                     METER_READING_PRESENT_VOLTAGE_L1, _reading->voltage_l1_v);
  meter_store_bind_double_if_present(stmt, 23, _reading->present_flags,
                                     METER_READING_PRESENT_VOLTAGE_L2, _reading->voltage_l2_v);
  meter_store_bind_double_if_present(stmt, 24, _reading->present_flags,
                                     METER_READING_PRESENT_VOLTAGE_L3, _reading->voltage_l3_v);
  meter_store_bind_double_if_present(stmt, 25, _reading->present_flags,
                                     METER_READING_PRESENT_CURRENT, _reading->current_a);
  meter_store_bind_double_if_present(stmt, 26, _reading->present_flags,
                                     METER_READING_PRESENT_CURRENT_L1, _reading->current_l1_a);
  meter_store_bind_double_if_present(stmt, 27, _reading->present_flags,
                                     METER_READING_PRESENT_CURRENT_L2, _reading->current_l2_a);
  meter_store_bind_double_if_present(stmt, 28, _reading->present_flags,
                                     METER_READING_PRESENT_CURRENT_L3, _reading->current_l3_a);
  meter_store_bind_double_if_present(stmt, 29, _reading->present_flags,
                                     METER_READING_PRESENT_FREQUENCY, _reading->frequency_hz);
  meter_store_bind_int64_if_present(stmt, 30, _reading->present_flags,
                                    METER_READING_PRESENT_VOLTAGE_SAG,
                                    _reading->voltage_sag_l1_count);
  meter_store_bind_int64_if_present(stmt, 31, _reading->present_flags,
                                    METER_READING_PRESENT_VOLTAGE_SAG,
                                    _reading->voltage_sag_l2_count);
  meter_store_bind_int64_if_present(stmt, 32, _reading->present_flags,
                                    METER_READING_PRESENT_VOLTAGE_SAG,
                                    _reading->voltage_sag_l3_count);
  meter_store_bind_int64_if_present(stmt, 33, _reading->present_flags,
                                    METER_READING_PRESENT_VOLTAGE_SWELL,
                                    _reading->voltage_swell_l1_count);
  meter_store_bind_int64_if_present(stmt, 34, _reading->present_flags,
                                    METER_READING_PRESENT_VOLTAGE_SWELL,
                                    _reading->voltage_swell_l2_count);
  meter_store_bind_int64_if_present(stmt, 35, _reading->present_flags,
                                    METER_READING_PRESENT_VOLTAGE_SWELL,
                                    _reading->voltage_swell_l3_count);
  meter_store_bind_int64_if_present(stmt, 36, _reading->present_flags,
                                    METER_READING_PRESENT_POWER_FAIL,
                                    _reading->any_power_fail_count);
  meter_store_bind_int64_if_present(stmt, 37, _reading->present_flags,
                                    METER_READING_PRESENT_POWER_FAIL,
                                    _reading->long_power_fail_count);
  meter_store_bind_double_if_present(stmt, 38, _reading->present_flags,
                                     METER_READING_PRESENT_AVERAGE_POWER_15M,
                                     _reading->average_power_15m_w);
  meter_store_bind_double_if_present(stmt, 39, _reading->present_flags,
                                     METER_READING_PRESENT_MONTHLY_POWER_PEAK,
                                     _reading->monthly_power_peak_w);
  meter_store_bind_text_if_present(stmt, 40, _reading->present_flags,
                                   METER_READING_PRESENT_MONTHLY_POWER_PEAK,
                                   _reading->monthly_power_peak_timestamp);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    SQL_CHECK_DB(_S->db, "step meter_readings insert failed");
    sqlite3_exec(_S->db, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_S->mutex);
    return ERR_FATAL;
  }

  sqlite3_finalize(stmt);
  sqlite3_exec(_S->db, "COMMIT;", NULL, NULL, NULL);
  pthread_mutex_unlock(&_S->mutex);

  return SUCCESS;
}

int meter_store_read_range(Meter_Reading_Store* _S, time_t _received_from, time_t _received_to,
                           Meter_Reading_Row** _rows_out, size_t* _count_out) {
  if (!_S || !_S->db || !_rows_out || !_count_out) {
    return ERR_INVALID_ARG;
  }

  const char* sql =
      "SELECT received_at, present_flags, unique_id, meter_model, timestamp, protocol_version, tariff,"
      " energy_import_kwh, energy_import_t1_kwh, energy_import_t2_kwh, energy_import_t3_kwh,"
      " energy_import_t4_kwh, energy_export_kwh, energy_export_t1_kwh, energy_export_t2_kwh,"
      " energy_export_t3_kwh, energy_export_t4_kwh, power_w, power_l1_w, power_l2_w, power_l3_w,"
      " voltage_v, voltage_l1_v, voltage_l2_v, voltage_l3_v, current_a, current_l1_a,"
      " current_l2_a, current_l3_a, frequency_hz, voltage_sag_l1_count, voltage_sag_l2_count,"
      " voltage_sag_l3_count, voltage_swell_l1_count, voltage_swell_l2_count,"
      " voltage_swell_l3_count, any_power_fail_count, long_power_fail_count,"
      " average_power_15m_w, monthly_power_peak_w, monthly_power_peak_timestamp "
      "FROM meter_readings "
      "WHERE received_at >= ? AND received_at < ? "
      "ORDER BY received_at ASC;";

  sqlite3_stmt* stmt = NULL;
  Meter_Reading_Row* rows = NULL;
  size_t count = 0;
  size_t capacity = 0;

  *_rows_out = NULL;
  *_count_out = 0;

  pthread_mutex_lock(&_S->mutex);

  if (sqlite3_prepare_v2(_S->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    SQL_CHECK_DB(_S->db, "prepare meter_readings range select failed");
    pthread_mutex_unlock(&_S->mutex);
    return ERR_FATAL;
  }

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)_received_from);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)_received_to);

  int step_res;
  while ((step_res = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (count == capacity) {
      size_t next_capacity = capacity == 0 ? 128 : capacity * 2;
      Meter_Reading_Row* next_rows =
          (Meter_Reading_Row*)realloc(rows, next_capacity * sizeof(Meter_Reading_Row));
      if (!next_rows) {
        free(rows);
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&_S->mutex);
        return ERR_NO_MEMORY;
      }
      rows = next_rows;
      capacity = next_capacity;
    }

    meter_store_read_stmt_row(stmt, &rows[count]);
    count++;
  }

  if (step_res != SQLITE_DONE) {
    SQL_CHECK_DB(_S->db, "step meter_readings range select failed");
    free(rows);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&_S->mutex);
    return ERR_FATAL;
  }

  sqlite3_finalize(stmt);
  pthread_mutex_unlock(&_S->mutex);

  *_rows_out = rows;
  *_count_out = count;
  return SUCCESS;
}

void meter_store_rows_dispose(Meter_Reading_Row** _rows, size_t* _count) {
  if (_rows && *_rows) {
    free(*_rows);
    *_rows = NULL;
  }

  if (_count) {
    *_count = 0;
  }
}

void meter_store_close(Meter_Reading_Store* _S) {
  if (_S && _S->db) {
    pthread_mutex_lock(&_S->mutex);
    sqlite3_close(_S->db);
    _S->db = NULL;
    pthread_mutex_unlock(&_S->mutex);
  }
}

void meter_store_dispose(Meter_Reading_Store* _S) {
  meter_store_close(_S);

  if (!_S) {
    return;
  }
  _S = NULL;
}

static void meter_store_copy_column_text(sqlite3_stmt* _stmt, int _col, char* _dest,
                                         size_t _size) {
  if (!_dest || _size == 0) {
    return;
  }

  _dest[0] = '\0';

  const unsigned char* text = sqlite3_column_text(_stmt, _col);
  if (!text) {
    return;
  }

  snprintf(_dest, _size, "%s", (const char*)text);
}

static void meter_store_bind_text_if_present(sqlite3_stmt* _stmt, int _idx, uint64_t _flags,
                                             uint64_t _present_flag, const char* _value) {
  if ((_flags & _present_flag) == 0 || !_value || _value[0] == '\0') {
    sqlite3_bind_null(_stmt, _idx);
    return;
  }

  sqlite3_bind_text(_stmt, _idx, _value, -1, SQLITE_STATIC);
}

static void meter_store_bind_int_if_present(sqlite3_stmt* _stmt, int _idx, uint64_t _flags,
                                            uint64_t _present_flag, int _value) {
  if ((_flags & _present_flag) == 0) {
    sqlite3_bind_null(_stmt, _idx);
    return;
  }

  sqlite3_bind_int(_stmt, _idx, _value);
}

static void meter_store_bind_int64_if_present(sqlite3_stmt* _stmt, int _idx, uint64_t _flags,
                                              uint64_t _present_flag, sqlite3_int64 _value) {
  if ((_flags & _present_flag) == 0) {
    sqlite3_bind_null(_stmt, _idx);
    return;
  }

  sqlite3_bind_int64(_stmt, _idx, _value);
}

static void meter_store_bind_double_if_present(sqlite3_stmt* _stmt, int _idx, uint64_t _flags,
                                               uint64_t _present_flag, double _value) {
  if ((_flags & _present_flag) == 0) {
    sqlite3_bind_null(_stmt, _idx);
    return;
  }

  sqlite3_bind_double(_stmt, _idx, _value);
}

static void meter_store_read_stmt_row(sqlite3_stmt* _stmt, Meter_Reading_Row* _row) {
  memset(_row, 0, sizeof(Meter_Reading_Row));

  _row->received_at = (time_t)sqlite3_column_int64(_stmt, 0);

  Meter_Reading* reading = &_row->reading;
  reading->present_flags = (uint64_t)sqlite3_column_int64(_stmt, 1);
  meter_store_copy_column_text(_stmt, 2, reading->unique_id, sizeof(reading->unique_id));
  meter_store_copy_column_text(_stmt, 3, reading->meter_model, sizeof(reading->meter_model));
  meter_store_copy_column_text(_stmt, 4, reading->timestamp, sizeof(reading->timestamp));
  reading->protocol_version       = (uint16_t)sqlite3_column_int(_stmt, 5);
  reading->tariff                 = (uint8_t)sqlite3_column_int(_stmt, 6);
  reading->energy_import_kwh      = sqlite3_column_double(_stmt, 7);
  reading->energy_import_t1_kwh   = sqlite3_column_double(_stmt, 8);
  reading->energy_import_t2_kwh   = sqlite3_column_double(_stmt, 9);
  reading->energy_import_t3_kwh   = sqlite3_column_double(_stmt, 10);
  reading->energy_import_t4_kwh   = sqlite3_column_double(_stmt, 11);
  reading->energy_export_kwh      = sqlite3_column_double(_stmt, 12);
  reading->energy_export_t1_kwh   = sqlite3_column_double(_stmt, 13);
  reading->energy_export_t2_kwh   = sqlite3_column_double(_stmt, 14);
  reading->energy_export_t3_kwh   = sqlite3_column_double(_stmt, 15);
  reading->energy_export_t4_kwh   = sqlite3_column_double(_stmt, 16);
  reading->power_w                = sqlite3_column_double(_stmt, 17);
  reading->power_l1_w             = sqlite3_column_double(_stmt, 18);
  reading->power_l2_w             = sqlite3_column_double(_stmt, 19);
  reading->power_l3_w             = sqlite3_column_double(_stmt, 20);
  reading->voltage_v              = sqlite3_column_double(_stmt, 21);
  reading->voltage_l1_v           = sqlite3_column_double(_stmt, 22);
  reading->voltage_l2_v           = sqlite3_column_double(_stmt, 23);
  reading->voltage_l3_v           = sqlite3_column_double(_stmt, 24);
  reading->current_a              = sqlite3_column_double(_stmt, 25);
  reading->current_l1_a           = sqlite3_column_double(_stmt, 26);
  reading->current_l2_a           = sqlite3_column_double(_stmt, 27);
  reading->current_l3_a           = sqlite3_column_double(_stmt, 28);
  reading->frequency_hz           = sqlite3_column_double(_stmt, 29);
  reading->voltage_sag_l1_count   = (uint32_t)sqlite3_column_int64(_stmt, 30);
  reading->voltage_sag_l2_count   = (uint32_t)sqlite3_column_int64(_stmt, 31);
  reading->voltage_sag_l3_count   = (uint32_t)sqlite3_column_int64(_stmt, 32);
  reading->voltage_swell_l1_count = (uint32_t)sqlite3_column_int64(_stmt, 33);
  reading->voltage_swell_l2_count = (uint32_t)sqlite3_column_int64(_stmt, 34);
  reading->voltage_swell_l3_count = (uint32_t)sqlite3_column_int64(_stmt, 35);
  reading->any_power_fail_count   = (uint32_t)sqlite3_column_int64(_stmt, 36);
  reading->long_power_fail_count  = (uint32_t)sqlite3_column_int64(_stmt, 37);
  reading->average_power_15m_w    = sqlite3_column_double(_stmt, 38);
  reading->monthly_power_peak_w   = sqlite3_column_double(_stmt, 39);
  meter_store_copy_column_text(_stmt, 40, reading->monthly_power_peak_timestamp,
                               sizeof(reading->monthly_power_peak_timestamp));
}
