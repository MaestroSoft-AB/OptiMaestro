#ifndef __OPTI_SERVER_INSTANCE_H__
#define __OPTI_SERVER_INSTANCE_H__

/* ******************************************************************* */
/* *************************** OPTI INSTANCE ************************* */
/* ******************************************************************* */

#include "http/http_connection.h"
#define API_ENDPOINT_ROOT "/api/v1"

typedef struct Opti_Server_Instance Opti_Server_Instance;
typedef struct
{
  void* ctx;
  Opti_Server_Instance* instance;
  HTTP_Server_Connection* conn;

} Osi_RequestCtx;

typedef int (*endpoint_function)(Osi_RequestCtx* _ctx);

typedef struct
{
  const char* path;
  HTTPMethod method;
  endpoint_function endpoint_func;

} Device_API_Endpoint;

typedef int (*osi_on_finish)(void* _context, void* _instance);

typedef enum
{
  OPTI_INSTANCE_INITIALIZING,
  OPTI_INSTANCE_IDLING,
  OPTI_INSTANCE_REQUEST_PARSING,
  OPTI_INSTANCE_RESPONSE_BUILDING,
  OPTI_INSTANCE_RESPONSE_SENDING,
  OPTI_INSTANCE_DISPOSING,
  OPTI_INSTANCE_ERROR

} OptiServerInstanceState;

struct Opti_Server_Instance
{
  Scheduler_Task* task;
  Linked_Item* item;
  void* context; // Weather_Server
  osi_on_finish on_finish;
  const Device_API_Endpoint* endpoint;
  HTTP_Server_Connection* http_connection;
  OptiServerInstanceState state;
};

int osi_init(void* _context, Opti_Server_Instance* _Instance, HTTP_Server_Connection* _Connection);
int osi_init_ptr(void* _context, HTTP_Server_Connection* _Connection,
                 Opti_Server_Instance** _Instance_Ptr);

void osi_dispose(Opti_Server_Instance* _Instance);
void osi_dispose_ptr(Opti_Server_Instance** _Instance_Ptr);

#endif
