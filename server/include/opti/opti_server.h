#ifndef __OPTI_SERVER_H__
#define __OPTI_SERVER_H__

/* ******************************************************************* */
/* ************************* WEATHER SERVER ************************** */
/* ******************************************************************* */

#include "http/http_server.h"

#define DATA_DIR "data/"

typedef enum
{
  OPTI_SERVER_INIT,
  OPTI_SERVER_IDLE,
  OPTI_SERVER_CONNECTING,
  OPTI_SERVER_CONNECTED,
  OPTI_SERVER_ERROR,
  OPTI_SERVER_DISPOSING

} OptiServerState;

typedef enum
{
  OPTI_SERVER_ERROR_NONE = 0,
  OPTI_SERVER_ERROR_INIT_FAILED,

} OptiServerErrorState;

typedef struct
{
  HTTP_Server http_server;
  HTTP_Server_Connection* http_connection; // temporary, hands over to instance
  Scheduler_Task* task;
  Linked_List* instances;
  OptiServerState state;

} Opti_Server;

int opti_s_init(Opti_Server* _Server);
int opti_s_init_ptr(Opti_Server** _Server_Ptr);
void opti_s_dispose(Opti_Server* _Server);
void opti_s_dispose_ptr(Opti_Server** _Server_Ptr);

#endif
