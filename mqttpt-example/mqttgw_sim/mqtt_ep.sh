#!/bin/bash

# ----------------------------------------------------------------------------
# Copyright 2018 ARM Ltd.
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ----------------------------------------------------------------------------

devicename="DEVNAME"
gweui="GWEUI"
temperature="23.4"
humidity="46.1"
set_point="22.5"

usage()
{
    echo "usage: mqtt_ep.sh [-d device-name] [-t temperature-value] [-h humidity-value] [-s set_point-value]"
}

ep_value_data()
{
    cat <<EOF
{
    "appeui": "00000000000000AB",
    "deveui": "$devicename",
    "cert_renewal": true,
    "metadata": {
        "code_rate": "4/5",
        "data_rate": "SF8BW500",
        "frequency": 923300000,
        "gateway": [
            {
                "chan": 0,
                "gweui": "$gweui",
                "rfchan": 0,
                "rssi": -23,
                "snr": 5,
                "time": " 117-0910T10:11:06Z"
            }
        ],
        "modulation": "MQTT",
        "port": 1,
        "seqno": 193
    },
    "payload_field": [
        {
            "payload_type": "VALUE",
            "objectid": "3303",
            "objectinstances": [{
                "objectinstance": "0",
                "resources": [{
                    "resourceid": "5700",
                    "resourcename": "Temperature",
                    "value": "$temperature",
                    "unit": "\u2103",
                    "minvalue": "-50",
                    "maxvalue": "50",
                    "operations": "5"
                }]
            }]
        },
        {
            "payload_type": "VALUE",
            "objectid":"3304",
            "objectinstances": [{
                "objectinstance": "0",
                "resources": [{
                    "resourceid": "5700",
                    "type": "Humidity",
                    "value": "$humidity",
                    "unit": "%",
                    "minvalue": "0",
                    "maxvalue": "100",
                    "operations": "5"
                }]
            }]

        },
        {
            "payload_type": "VALUE",
            "objectid": "3308",
            "objectinstances": [{
                "objectinstance": "0",
                "resources": [{
                    "resourceid": "5900",
                    "resoucename": "Set point",
                    "value": "$set_point",
                    "unit": "\u2103",
                    "minvalue": "-50",
                    "maxvalue": "50",
                    "operations": "3"
                }]
            }]
        }
    ],
    "payload_raw": "",
    "remark": "mqtt_test"
}
EOF
}

while [ "$1" != "" ]; do
    case $1 in
        -d | --device )         shift
                                devicename=$1
                                ;;
        -t | --temperature )    shift
                                temperature=$1
                                ;;
        -h | --humidity )       shift
                                humidity=$1
                                ;;
        -s | --set-point )      shift
                                set_point=$1
                                ;;
        * )                     usage
                                exit 1
    esac
    shift
done


echo "$(ep_value_data)"
mosquitto_pub -t MQTTGw/$gweui/Node/$devicename/EdgeVal -m "$(ep_value_data)"
