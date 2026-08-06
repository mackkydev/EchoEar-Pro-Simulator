#ifndef ECHOEAR_CREDENTIALS_STORE_MOCK_H
#define ECHOEAR_CREDENTIALS_STORE_MOCK_H

#include <stdbool.h>

bool echoear_credentials_store_mock_load(
    const char *config_path,
    const char *device_storage_path);

#endif
