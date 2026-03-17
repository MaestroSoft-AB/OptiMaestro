#include "sqlite_helpers.h"
#include <maestroutils/error.h>
#include <stdio.h>
#include <stdlib.h>

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

  const char* sql = "CREATE TABLE IF NOT EXISTS electricity_spots ("
                    " time_start INTEGER NOT NULL,"
                    " time_end INTEGER NOT NULL,"
                    " spot_price REAL NOT NULL,"
                    " price_class INTEGER NOT NULL,"
                    " currency INTEGER NOT NULL,"
                    " PRIMARY KEY (time_start, price_class, currency)"
                    ");"
                    "CREATE INDEX IF NOT EXISTS idx_spots_series_time "
                    "ON electricity_spots(price_class, currency, time_start);";

  char* err = NULL;

  if (sqlite3_exec(_db, sql, NULL, NULL, &err) != SQLITE_OK) {
    sqlite3_free(err);
    return ERR_FATAL;
  }

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
