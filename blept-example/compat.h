#ifndef __MEPT_BLE_COMPAT_H
#define __MEPT_BLE_COMPAT_H

#define UUID_FMT \
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"

#define FORMATTED_UUID_LEN 36

#define JSON_DEVICE_FMT "{\"services\":["
#define JSON_SRVC_FMT "%s{\"uuid\":\"%s\",\"path\":\"/%d/%d\",\"characteristics\":["
#define JSON_CHAR_FMT "%s{\"uuid\":\"%s\",\"path\":\"/%d/%d/%d\"}"
#define JSON_ARRAY_END "]}"

#define JSON_FORMATTED_DEV_LEN (sizeof(JSON_DEVICE_FMT) + sizeof(JSON_ARRAY_END)) /* sizeof includes terminating zero for string literals */
#define JSON_FORMATTED_SRVC_LEN (FORMATTED_UUID_LEN + sizeof(JSON_SRVC_FMT) + sizeof(JSON_ARRAY_END)) /* slightly overshot but convenient calculation */
#define JSON_FORMATTED_CHAR_LEN (FORMATTED_UUID_LEN + sizeof(JSON_CHAR_FMT) + sizeof(JSON_ARRAY_END))

#endif /* __MEPT_BLE_COMPAT_H */
