# OptiMaestro
Initial repo setup for the OptiMaestro project

===============================================
## config mental model:

- config_handler.c   → HOW to read config
- server.conf        → WHAT the config is
- server.c           → USES the config

## upstream mental model

[ Client ]
     ↓
[ Optimaestro ]
     ↓
[ Upstream Servers ]


- Optimaestro distributes incoming requests:

Request 1 → 10.0.0.1
Request 2 → 10.0.0.2
Request 3 → 10.0.0.3


This prevents one server from being overloaded.

- Failover / redundancy

If one upstream fails:

10.0.0.2 ❌


Optimaestro can skip it and continue using:

10.0.0.1, 10.0.0.3


No code change needed just update config.
===============================================