/*
 * ----------------------------------------------------------------------------
 * Copyright 2018 ARM Ltd.
 * ----------------------------------------------------------------------------
 */

#include <pthread.h>
#include <pt-client/pt_api.h>

void device_registration_success(const char* device_id, void *userdata);
void device_registration_failure(const char* device_id, void *userdata);
void device_unregistration_success(const char* device_id, void *userdata);
void device_unregistration_failure(const char* device_id, void *userdata);

