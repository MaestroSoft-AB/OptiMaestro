#include "elprisjustnu.h"
#include "maestroutils/error.h"
#include "maestroutils/json_utils.h"

int elprisjustnu_init(Elprisjustnu_Spots* _EPJN)
{

  return SUCCESS;
}

/** Call elprisjustnu API to build struct values */
int elprisjustnu_update(Elprisjustnu_Spots* _EPJN)
{

  return SUCCESS;
}

int elprisjustnu_parse(Elprisjustnu_Spots* _EPJN, Electricity_Spots* _Spot)
{

  return SUCCESS;
}

void elprisjustnu_dispose(Elprisjustnu_Spots* _EPJN)
{

  _EPJN = NULL;
}
