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

usage()
{
    echo "usage: mqtt_ep.sh [-d device-name] [-t temperature-value] [-h humidity-value]"
}

ep_value_data()
{
    cat <<EOF
{
    "appeui": "00000000000000AB",
    "deveui": "$devicename",
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
        [
            "VALUE",
            "Temperature",
            "$temperature",
            "\u2103",
            "-50",
            "50",
            "1"
        ],
        [
            "VALUE",
            "Humidity",
            "$humidity",
            "%",
            "0",
            "100",
            "2"
        ]
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
        * )                     usage
                                exit 1
    esac
    shift
done


echo "$(ep_value_data)"
mosquitto_pub -t MQTTGw/$gweui/Node/$devicename/Val -m "$(ep_value_data)"
