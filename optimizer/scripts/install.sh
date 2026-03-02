#!/bin/bash

# Ensure script is run with sudo
if [ "$EUID" -ne 0 ]; then
  echo "This script must be run as root (use sudo)." >&2
  exit 1
fi

CONF_DIR="/etc/maestro"
mkdir -p "$CONF_DIR"

CONF_FILE="$CONF_DIR/optimizer.conf"
CONF_CONTENT='
################# System ################# 
sys.max_threads=4

################# Logging ################
log.level=INFO
log.path=/var/log/maestro.log

################## Data ##################
data.dir=/var/lib/maestro/
data.spots.dir=/var/lib/maestro/spots/
data.weather.dir=/var/lib/maestro/weather/
data.calcs.dir=/var/lib/maestro/calcs/

data.type=json

data.spots.currency=SEK

################ Facility ################
facility.panel.tilt=45
facility.panel.azimuth=180
'

# Check if config file exists, create if not
if [ ! -f "$CONF_FILE" ]; then
  echo "$CONF_CONTENT" > "$CONF_FILE"
  echo "Created $CONF_FILE with default content."
else
  echo "$CONF_FILE already exists, skipping."
fi

