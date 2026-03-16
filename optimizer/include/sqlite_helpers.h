#ifndef __SQLITE_HELPERS_H__
#define __SQLITE_HELPERS_H__

#include "data/electricity_structs.h"
#include <sqlite3.h>
#include <time.h>

int sql_helper_open(sqlite3** _db, const char* _path);
void sql_helper_close(sqlite3* _db);
int sql_helper_init_schema(sqlite3* _db);
int sql_helper_insert_spots(sqlite3* _db, const Electricity_Spots* _spot);
int sql_helper_read_spots(sqlite3* _db, Electricity_Spots* _out, SpotPriceClass _price_class,
                          SpotCurrency _currency, time_t _start, time_t _end);

#endif
