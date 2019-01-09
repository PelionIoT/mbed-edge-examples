/*
 * ----------------------------------------------------------------------------
 * Copyright 2018 ARM Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ----------------------------------------------------------------------------
 */

#ifndef _PT_BLE_SUPPORTED_TRANSLATIONS_H_
#define _PT_BLE_SUPPORTED_TRANSLATIONS_H_

#include "pt_ble_translations.h"

typedef struct dbus_resource_mapping dbus_resource_mapping_t;
typedef struct ble_characteristic ble_characteristic_t;

extern const size_t ble_services_count;
extern const ble_service_t ble_services[];

/**
 * Device information service constructor.
 */
bool ble_services_construct_dis_service(struct ble_device *device,
                                        const int sv_idx,
                                        const ble_service_t *service_descriptor);

/**
 * Sensor characteristic translations (for eg. environmental sensing service)
 */
bool ble_services_construct_temperature_characteristic(struct ble_device *device,
                                                       const int sv_idx,
                                                       const int ch_idx,
                                                       const ble_characteristic_t *ble_characteristic);
bool ble_services_construct_humidity_characteristic(struct ble_device *device,
                                                    const int sv_idx,
                                                    const int ch_idx,
                                                    const ble_characteristic_t *ble_characteristic);
bool ble_services_construct_barometer_characteristic(struct ble_device *device,
                                                     const int sv_idx,
                                                     const int ch_idx,
                                                     const ble_characteristic_t *ble_characteristic);

/**
 * Automation IO service translation. See 2-bit encoding and decoding functions for reference.
 */
bool ble_services_construct_automation_io_characteristic(struct ble_device *device,
                                                         const int sv_idx,
                                                         const int ch_idx,
                                                         const ble_characteristic_t *ble_characteristic);
bool ble_services_construct_pushbutton_from_characteristic(struct ble_device *device,
                                                           const int sv_idx,
                                                           const int ch_idx,
                                                           const ble_characteristic_t *ble_characteristic);

/**
 * \brief Generic 2-bit bitfield value encoding function.
 *        See Automation IO's light control translation for reference.
 */
bool ble_services_encode_2bit_bitfield_value(const struct ble_device *device,
                                             const struct translation_context *ctx,
                                             const uint8_t *current_characteristic_value,
                                             const size_t current_characteristic_size,
                                             const uint8_t *new_value,
                                             const size_t new_size,
                                             uint8_t **new_characteristic_value,
                                             size_t *new_characteristic_size);
/**
 * \brief Generic 2-bit bitfield value decoding function.
 *        See Automation IO's pushbutton translation for reference.
 */
bool ble_services_decode_2bit_bitfield_value(const struct ble_device *device,
                                             const struct translation_context *ctx,
                                             const uint8_t *value,
                                             const size_t value_size,
                                             uint8_t **value_out,
                                             size_t *value_out_len);

/**
 * \brief Generic value decoder using the format descriptor.
 *        Can be used to decode for example most of the numerical types. See the
 *        ble_services_characteristic_value_format_t struct definition for reference how to
 *        define the value format.
 */
bool ble_services_decode_value_with_format_descriptor(const struct ble_device *device,
                                                      const struct translation_context *ctx,
                                                      const uint8_t *value,
                                                      const size_t value_size,
                                                      uint8_t **value_out,
                                                      size_t *value_out_len);

#endif /* _PT_BLE_SUPPORTED_TRANSLATIONS_H_ */
