# OptiMaestro
Initial repo setup for the OptiMaestro project

**OptiMaestro** is a utility for optimizing electricity consumption based on real-time price and weather data. It integrates information from two sources — **elprisjustnu** (electricity pricing API) and **open-meteo** (weather and solar data) — to determine when during the day it’s cheapest or most efficient to consume or produce electricity, especially for setups that include solar panels.

---

## Overview

The optimizer evaluates pricing and weather data, computes optimal usage intervals, and provides actionable insights for managing power-hungry devices or energy storage systems. Its modular design relies on the **MaestroCore** framework and can optionally include JSON utilities for extended data handling.

The application can run as a standard command-line tool or as a background service (daemon) for continuous monitoring.

---

## Features

- Fetches real-time electricity and weather data from external APIs.  
- Calculates the cheapest hours of the day to use power.  
- Supports solar panel data integration for improved optimization.  
- Can be run interactively or as a persistent system daemon.  
- Configurable installation paths and build options through a flexible Makefile.  


## Prerequisites

Make sure the following components are installed on your system:
    - GNU Make, CMake, and GCC (recent versions)
    - Linux (recommended and tested environment)
    - Submodules initialized (`MaestroCore`, `cJSON`)

Initialize submodules:
```bash
git submodule update --init --recursive
```

### Building the project

The main Makefile provides a modular, dependency-aware build system that automatically handles MaestroCore and all project modules (server, optimizer).

- Build individual modules:
```bash
make optimizer
make server
```
These can be expanded to build using their indivudual recipes:
```bash
make optimizer/install
make optimizer/daemon-install
make server/run
```

- Clean build artifacts:
```bash
make clean
```
