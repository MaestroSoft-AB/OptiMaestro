#!/bin/bash

# Ensure script is run with sudo
if [ "$EUID" -ne 0 ]; then
  echo "This script must be run as root (use sudo)." >&2
  exit 1
fi

OPTI_CONF_DIR="/etc/maestro"
FAC_CONF_DIR="/etc/maestro/facility"
mkdir -p "$OPTI_CONF_DIR"
mkdir -p "$FAC_CONF_DIR"

cp -r ../config/examples/* $FAC_CONF_DIR

# Check if config file exists, create if not
if [ ! -f "$CONF_FILE" ]; then
  cp ../config/optimizer.conf $OPTI_CONF_DIR
  echo "Created $CONF_FILE with default content."
else
  echo "$CONF_FILE already exists, skipping."
fi

