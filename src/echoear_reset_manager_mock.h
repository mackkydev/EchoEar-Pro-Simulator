#ifndef ECHOEAR_RESET_MANAGER_MOCK_H
#define ECHOEAR_RESET_MANAGER_MOCK_H

#include <stdbool.h>

bool echoear_reset_manager_mock_load(
    const char *reset_path,
    const char *device_storage_path);

#endif
