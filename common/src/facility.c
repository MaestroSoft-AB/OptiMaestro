#include "data/facility.h"
#include "data/electricity_structs.h"
#include <maestroutils/file_utils.h>
#include <maestroutils/file_logging.h>
#include <maestroutils/config_handler.h>

static inline Facility_Config* facility_parse_config(const char* _filepath)
{
  if (!_filepath)
    return NULL;

  Facility_Config* Conf = calloc(1, sizeof(Facility_Config));
  if (!Conf) {
    LOG_ERROR("calloc");
    return NULL;
  }

  const int KEYS_COUNT = 8;
  const char* keys[] = {
    "name",
    "latitude",
    "longitude",
    "currency",
    "price_class",
    "panel.tilt",
    "panel.azimuth",
    "panel.m2_size",
  };

  char* values[KEYS_COUNT];
  int vals_found = config_values_get(_filepath, keys, values, KEYS_COUNT);

  /* First five keys are obligatory, MAKE THIS SAFER IF EDITING ABOVE CODE */
  for (int i = 0; i < 5; i++) {
    if (values[i] == NULL) {
      LOG_ERROR("config_values_get - One or more obligatory fields not found! (found: %i)", 
          vals_found);
      config_values_dispose(values, KEYS_COUNT);
      return NULL;
    }
  }

  /* Bind names to entries in values[] */
  char* conf_name          = values[0];
  char* conf_lat           = values[1];
  char* conf_lon           = values[2];
  // char* conf_currency      = values[3];
  char* conf_price_class   = values[4];
  char* conf_panel_tilt    = values[5];
  char* conf_panel_azimuth = values[6];
  char* conf_panel_size    = values[7];

  /* Get name */
  size_t name_len = strlen(conf_name);
  Conf->name = malloc(name_len + 1);
  if (!Conf->name) {
    LOG_ERROR("malloc");
    config_values_dispose(values, KEYS_COUNT);
    return NULL;
  }
  memcpy(Conf->name, conf_name, name_len);
  Conf->name[name_len] = '\0';

  // TODO: maybe handle atof/atoi better to mitigate unexpected results later
  float lat = atof(conf_lat);
  float lon = atof(conf_lon);
  Conf->lat = lat;
  Conf->lon = lon;

  /* You don't get to choose currency >:) */
  Conf->currency = SPOT_SEK;

  int price_class = atoi(conf_price_class) - 1;
  if (price_class >= 0 && price_class <= 3)
    Conf->price_class = price_class;
  else
    Conf->price_class = SE1;

  /* If we got solar panel settings, we define it in config struct */
  if (conf_panel_tilt != NULL && conf_panel_azimuth != NULL && conf_panel_size != NULL) {
    Conf->panel = calloc(1, sizeof(Solar_Panel));
    if (!Conf->name) {
      LOG_ERROR("calloc");
      config_values_dispose(values, KEYS_COUNT);
      return NULL;
    }
    int panel_tilt =    atoi(conf_panel_tilt);
    int panel_azimuth = atoi(conf_panel_azimuth);
    int panel_size =    atoi(conf_panel_size);
    Conf->panel->tilt = (unsigned short)panel_tilt;
    Conf->panel->azimuth = (short)panel_azimuth;
    Conf->panel->size = (unsigned short)panel_size;
  }
  else // There is no panel
    Conf->panel = NULL;

  config_values_dispose(values, KEYS_COUNT);
  LOG_INFO("Config for facility %s parsed from: %s", Conf->name, _filepath);
  // LOG_INFO("latitude: %f, longitude %f\n", Conf->lon, Conf->lat);
  // LOG_INFO("azimuth %i, tilt: %i\n", Conf->panel_azimuth, Conf->panel_tilt);
  return Conf;
}

Facility_Config** facility_get_configs(const char* _facility_dir, size_t* _facility_count) 
{
  if (!_facility_dir) 
    return NULL;

  Facility_Config** Conf_Array = malloc(sizeof(Facility_Config*));
  if (!Conf_Array) {
    LOG_ERROR("malloc");
    return NULL;
  }

  size_t conf_files_count = 0;
  char** conf_files = list_filenames_with_ext(_facility_dir, "conf", &conf_files_count);

  if (!conf_files) {
    free(Conf_Array);
    LOG_ERROR("list_filenames_with_ext");
    return NULL;
  }

  for (int i = 0; i < (int)conf_files_count; i++) {
    /* Reallocate siz of struct array */
    Conf_Array = realloc(Conf_Array, (sizeof(Facility_Config) * (i + 1)));
    if (!Conf_Array) {
      LOG_ERROR("realloc");
      free(Conf_Array);

      /* Dispose of the rest of conf file names */
      for (int y = i; y < (int)conf_files_count; y++)
        free(conf_files);

      return NULL;
    }

    /* Define full path */
    char filepath[256];
    size_t path_len = snprintf(filepath, sizeof(filepath), 
      "%s/%s", _facility_dir, conf_files[i]);
    if (path_len < 1) {
      LOG_ERROR("snprintf");
      facility_dispose(Conf_Array, i+1);

      /* Dispose of the rest of conf name strings */
      for (int y = i; y < (int)conf_files_count; y++)
        free(conf_files);

      return NULL;
    }

    /* Parse config */
    Facility_Config* Conf = facility_parse_config(filepath);
    if (!Conf) {
      LOG_ERROR("facility_parse_config");
      facility_dispose(Conf_Array, i+1);

      /* Dispose of the rest of conf name strings */
      for (int y = i; y < (int)conf_files_count; y++)
        free(conf_files);

      return NULL;
    }

    (*_facility_count)++;

    Conf_Array[i] = Conf;
    free(conf_files[i]); // free conf name string
  }
  free(conf_files);

  return Conf_Array; 
}

void facility_dispose(Facility_Config** _Configs, size_t _facility_count) 
{
  if (!_Configs) return;

  for (size_t i = 0; i < _facility_count; i++) {
    if (_Configs[i] != NULL) {

      if (_Configs[i]->panel != NULL)
        free(_Configs[i]->panel);
      if (_Configs[i]->name != NULL)
        free(_Configs[i]->name);

      free(_Configs[i]);
      _Configs[i] = NULL;
    }
  }

  free(_Configs);
  _Configs = NULL;
}
