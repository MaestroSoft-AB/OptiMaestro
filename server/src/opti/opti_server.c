#include "opti/opti_server.h"
#include "opti/opti_instance.h"
#include <maestroutils/file_utils.h>

/* -----------------Internal Functions----------------- */

void opti_s_taskwork(void* _context, uint64_t _montime);
int opti_s_on_http_connection(void* _context, HTTP_Server_Connection* _Connection);
int opti_s_on_instance_finish(void* _context, void* _instance);

OptiServerState opti_server_connection_handover(Opti_Server* _Server);
/* ---------------------------------------------------- */

int opti_s_init(Opti_Server* _Server)
{

  if (!_Server) {
    return ERR_INVALID_ARG;
  }

  /*_Server->http_server = NULL;*/
  _Server->instances = NULL;
  _Server->task = NULL;
  _Server->state = OPTI_SERVER_INIT;
  _Server->http_connection = NULL;

  int result = http_server_init(&_Server->http_server, opti_s_on_http_connection, _Server);
  if (result != SUCCESS) {

    _Server->state = OPTI_SERVER_ERROR;

    return result;
  }

  Linked_List* Instances = linked_list_create();

  if (!Instances) {
    http_server_dispose(&_Server->http_server);
    _Server->state = OPTI_SERVER_ERROR;
    return ERR_NO_MEMORY;
  }

  _Server->instances = Instances;
  _Server->task = scheduler_create_task(_Server, opti_s_taskwork);
  if (!_Server->task) {
    linked_list_destroy(&_Server->instances);
    http_server_dispose(&_Server->http_server);
    _Server->state = OPTI_SERVER_ERROR;
    return ERR_FATAL;
  }

  _Server->state = OPTI_SERVER_IDLE;

  create_directory_if_not_exists(DATA_DIR);

  return SUCCESS;
}

int opti_s_init_ptr(Opti_Server** _Server_Ptr)
{

  if (!_Server_Ptr) {
    return ERR_INVALID_ARG;
  }

  Opti_Server* Server = calloc(1, sizeof(Opti_Server));
  if (!Server) {
    return ERR_NO_MEMORY;
  }

  int result = opti_s_init(Server);
  if (result != SUCCESS) {
    free(Server);
    return result;
  }

  *_Server_Ptr = Server;

  return SUCCESS;
}

/* --------------TASKWORK STATE FUNCTIONS-------------- */

/* ---------------------------------------------------- */

int opti_s_on_http_connection(void* _context, HTTP_Server_Connection* _Connection)
{
  if (!_context || !_Connection) {
    return ERR_INVALID_ARG;
  }

  Opti_Server* Server = (Opti_Server*)_context;
  Server->http_connection = _Connection;

  Server->state = OPTI_SERVER_CONNECTING;
  return SUCCESS;
}

OptiServerState opti_server_connection_handover(Opti_Server* _Server)
{
  if (!_Server) {
    return OPTI_SERVER_ERROR;
  }

  Opti_Server_Instance* Instance = NULL;

  int result = osi_init_ptr(_Server, _Server->http_connection, &Instance);
  if (result != SUCCESS) {
    return OPTI_SERVER_ERROR;
  }

  Linked_Item* LI;
  linked_list_item_add(_Server->instances, &LI, Instance);
  Instance->item = LI;

  Instance->on_finish = opti_s_on_instance_finish;

  _Server->http_connection = NULL;
  return OPTI_SERVER_CONNECTED;
}

int opti_s_on_instance_finish(void* _context, void* _instance)
{
  if (!_context || !_instance) {
    return ERR_INVALID_ARG;
  }

  Opti_Server* Server = (Opti_Server*)_context;
  Opti_Server_Instance* Instance = (Opti_Server_Instance*)_instance;
  if (Instance->item != NULL)
    linked_list_item_remove(Server->instances, Instance->item);

  linked_list_foreach(Server->instances, node) printf("Instance: %p\n", node);

  osi_dispose_ptr(&Instance);

  return SUCCESS;
}

int opti_s_on_http_error(void* _context)
{
  if (!_context) {
    return ERR_INVALID_ARG;
  }

  Opti_Server* server = (Opti_Server*)_context;
  server->state = OPTI_SERVER_DISPOSING;

  return SUCCESS;
}

void opti_s_taskwork(void* _context, uint64_t _MonTime)
{
  (void)_MonTime;
  if (!_context)
    return;

  Opti_Server* server = (Opti_Server*)_context;

  OptiServerState next_state = server->state;

  switch (server->state) {
  case OPTI_SERVER_INIT:
    printf("OPTI_SERVER_INIT\n");
    next_state = OPTI_SERVER_IDLE;
    break;

  case OPTI_SERVER_IDLE: {
    break;
  }

  case OPTI_SERVER_CONNECTING: {
    next_state = opti_server_connection_handover(server);
    break;
  }

  case OPTI_SERVER_CONNECTED:
    printf("OPTI_SERVER_CONNECTED\n");
    next_state = OPTI_SERVER_IDLE;
    break;

  case OPTI_SERVER_ERROR:
    printf("OPTI_SERVER_ERROR\n");
    next_state = OPTI_SERVER_DISPOSING;
    break;

  case OPTI_SERVER_DISPOSING:
    opti_s_dispose(server);
    /*CALL DISPOSE STUFF HERE*/
    printf("OPTI_SERVER_DISPOSE\n");
    break;
  }

  server->state = next_state;
}

void opti_s_dispose(Opti_Server* _Server)
{
  if (!_Server) {
    return;
  }

  if (_Server->instances) {
    linked_list_destroy(&_Server->instances);
  }
  http_server_dispose(&_Server->http_server);

  if (_Server->task) {
    scheduler_destroy_task(_Server->task);
  }
}
