#ifndef __METER_READING_STORE_H__
#define __METER_READING_STORE_H__

#include "data/meter_reading.h"
#include <pthread.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define METER_READING_STORE_DEFAULT_DIR "/var/lib/maestro/meter_readings"
#define METER_READING_STORE_DB_FILENAME "meter_readings.db"
#define METER_READING_STORE_DEFAULT_DB_PATH \
  METER_READING_STORE_DEFAULT_DIR "/" METER_READING_STORE_DB_FILENAME

typedef struct
{
  sqlite3*        db;
  pthread_mutex_t mutex;
} Meter_Reading_Store;

typedef struct
{
  time_t        received_at;
  Meter_Reading reading;
} Meter_Reading_Row;

int  meter_store_init(Meter_Reading_Store* _S);
int  meter_store_open(Meter_Reading_Store* _S, const char* _path, bool _readonly);
void meter_store_close(Meter_Reading_Store* _S);
int  meter_store_init_schema(Meter_Reading_Store* _S);
int  meter_store_insert_reading(Meter_Reading_Store* _S, const Meter_Reading* _reading);
int  meter_store_read_range(Meter_Reading_Store* _S, time_t _received_from, time_t _received_to,
                            Meter_Reading_Row** _rows_out, size_t* _count_out);
void meter_store_rows_dispose(Meter_Reading_Row** _rows, size_t* _count);
void meter_store_dispose(Meter_Reading_Store* _S);

#endif
