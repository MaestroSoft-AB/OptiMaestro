#ifndef __METER_READING_H__
#define __METER_READING_H__

#include <stdint.h>

/* ======================= HomeWizard P1 Reading ======================= */

#define METER_READING_ID_LEN 64
#define METER_READING_MODEL_LEN 64
#define METER_READING_TIMESTAMP_LEN 20

#define METER_READING_PRESENT_ID (1ULL << 0)
#define METER_READING_PRESENT_PROTOCOL_VERSION (1ULL << 1)
#define METER_READING_PRESENT_MODEL (1ULL << 2)
#define METER_READING_PRESENT_TIMESTAMP (1ULL << 3)
#define METER_READING_PRESENT_TARIFF (1ULL << 4)
#define METER_READING_PRESENT_ENERGY_IMPORT (1ULL << 5)
#define METER_READING_PRESENT_ENERGY_IMPORT_T1 (1ULL << 6)
#define METER_READING_PRESENT_ENERGY_IMPORT_T2 (1ULL << 7)
#define METER_READING_PRESENT_ENERGY_IMPORT_T3 (1ULL << 8)
#define METER_READING_PRESENT_ENERGY_IMPORT_T4 (1ULL << 9)
#define METER_READING_PRESENT_ENERGY_EXPORT (1ULL << 10)
#define METER_READING_PRESENT_ENERGY_EXPORT_T1 (1ULL << 11)
#define METER_READING_PRESENT_ENERGY_EXPORT_T2 (1ULL << 12)
#define METER_READING_PRESENT_ENERGY_EXPORT_T3 (1ULL << 13)
#define METER_READING_PRESENT_ENERGY_EXPORT_T4 (1ULL << 14)
#define METER_READING_PRESENT_POWER (1ULL << 15)
#define METER_READING_PRESENT_POWER_L1 (1ULL << 16)
#define METER_READING_PRESENT_POWER_L2 (1ULL << 17)
#define METER_READING_PRESENT_POWER_L3 (1ULL << 18)
#define METER_READING_PRESENT_VOLTAGE (1ULL << 19)
#define METER_READING_PRESENT_VOLTAGE_L1 (1ULL << 20)
#define METER_READING_PRESENT_VOLTAGE_L2 (1ULL << 21)
#define METER_READING_PRESENT_VOLTAGE_L3 (1ULL << 22)
#define METER_READING_PRESENT_CURRENT (1ULL << 23)
#define METER_READING_PRESENT_CURRENT_L1 (1ULL << 24)
#define METER_READING_PRESENT_CURRENT_L2 (1ULL << 25)
#define METER_READING_PRESENT_CURRENT_L3 (1ULL << 26)
#define METER_READING_PRESENT_FREQUENCY (1ULL << 27)
#define METER_READING_PRESENT_VOLTAGE_SAG (1ULL << 28)
#define METER_READING_PRESENT_VOLTAGE_SWELL (1ULL << 29)
#define METER_READING_PRESENT_POWER_FAIL (1ULL << 30)
#define METER_READING_PRESENT_AVERAGE_POWER_15M (1ULL << 31)
#define METER_READING_PRESENT_MONTHLY_POWER_PEAK (1ULL << 32)

typedef struct
{
  uint64_t present_flags;

  char unique_id[METER_READING_ID_LEN];
  char meter_model[METER_READING_MODEL_LEN];
  char timestamp[METER_READING_TIMESTAMP_LEN]; // ISO 8601 local time, no timezone

  uint16_t protocol_version;
  uint8_t  tariff;

  double energy_import_kwh;
  double energy_import_t1_kwh;
  double energy_import_t2_kwh;
  double energy_import_t3_kwh;
  double energy_import_t4_kwh;
  double energy_export_kwh;
  double energy_export_t1_kwh;
  double energy_export_t2_kwh;
  double energy_export_t3_kwh;
  double energy_export_t4_kwh;

  double power_w;
  double power_l1_w;
  double power_l2_w;
  double power_l3_w;

  double voltage_v;
  double voltage_l1_v;
  double voltage_l2_v;
  double voltage_l3_v;

  double current_a;
  double current_l1_a;
  double current_l2_a;
  double current_l3_a;

  double frequency_hz;

  uint32_t voltage_sag_l1_count;
  uint32_t voltage_sag_l2_count;
  uint32_t voltage_sag_l3_count;
  uint32_t voltage_swell_l1_count;
  uint32_t voltage_swell_l2_count;
  uint32_t voltage_swell_l3_count;
  uint32_t any_power_fail_count;
  uint32_t long_power_fail_count;

  double average_power_15m_w;
  double monthly_power_peak_w;
  char   monthly_power_peak_timestamp[METER_READING_TIMESTAMP_LEN];

} Meter_Reading;

#endif
