#include "optimizer.h"
#include "electricity_cache_handler.h"
#include "error.h"

#include <stdio.h>
#include <pthread.h>

pthread_mutex_t optimizer_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct 
{
  pthread_mutex_t mutex;
  pthread_t*      tptr;
  void*           context;
  int             thread_no;

} Thread_Args;

void* thread_func(void* _arg)
{
  Thread_Args* Args = (Thread_Args*)_arg; 
  int res;

  pthread_mutex_lock(&Args->mutex);

  if (Args->thread_no == 1)
  {
    res = ech_update_cache(Args->context);
  } else if (Args->thread_no == 2)
  {
    res = wch_update_cache(Args->context);
  }
    
  // printf("t%i - count: %i\r\n", Args->thread_no, counter++);
  pthread_mutex_unlock(&Args->mutex);

  return NULL; //return result somehow to skip need for struct result
}

/* */
int optimizer_init(Optimizer* _OC)
{
  optimizer_config_set(_OC, OPTI_CONFIG_PATH);
  return SUCCESS;
}

int optimizer_config_set(Optimizer* _OC, const char* _path)
{
  // TODO: read from conf file, add conf parse util

  _OC->config.max_threads = 2;

  return SUCCESS;
}

int optimizer_run(Optimizer* _OC)
{
  int i, res;
  int max_threads = _OC->config.max_threads;

  /* Init cache handlers */
  res = wch_init(_OC->wch);
  if (res != 0) {
    perror("wch_init");
    return res;
  }
  res = ech_init(_OC->ech);
  if (res != 0) {
    perror("ech_init");
    return res;
  }

  if (_OC->config.max_threads < 2)
  {
    /* run one by one */
    res = wch_update_cache(_OC->wch);
    if (res != 0) {
      perror("wch_update_cache");
      return res;
    }
    res = ech_update_cache(_OC->ech);
    if (res != 0) {
      perror("ech_update_cache");
      return res;
    }
  }
  else
  {
    /* run threaded  */
    pthread_t echt, wcht;

    Thread_Args Cache_Threads[2] = {
      { optimizer_mutex, &echt, (void*)_OC->ech, 1, },
      { optimizer_mutex, &wcht, (void*)_OC->wch, 2, },
    };

    res = pthread_create(&echt, NULL, thread_func, &Cache_Threads[0]);
    if (res != 0)
    {
      perror("pthread_create");
      return res;
    }

    res = pthread_create(&wcht, NULL, thread_func, &Cache_Threads[1]);
    if (res != 0)
    {
      perror("pthread_create");
      return res;
    }

    pthread_join(echt, NULL);
    pthread_join(wcht, NULL);
  }

  return SUCCESS;
}

int optimizer_threads_init()
{

  return SUCCESS;
}

void optimizer_dispose(Optimizer* _OC)
{

}
