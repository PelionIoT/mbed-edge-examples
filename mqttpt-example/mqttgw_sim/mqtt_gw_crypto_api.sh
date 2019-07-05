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

usage()
{
    echo "usage: mqtt_gw_crypto_api.sh renew-certificate <name>"
    echo "usage: mqtt_gw_crypto_api.sh set-certificates-list <names>..."
    echo "usage: mqtt_gw_crypto_api.sh get-certificate <name>"
    echo "usage: mqtt_gw_crypto_api.sh get-public-key <name>"
    echo "usage: mqtt_gw_crypto_api.sh generate-random <size>"
    echo "usage: mqtt_gw_crypto_api.sh asymmetric-sign <private-key-name> <hash-digest>"
    echo "usage: mqtt_gw_crypto_api.sh asymmetric-verify <public-key-name> <hash-digest> <signature>"
    echo "usage: mqtt_gw_crypto_api.sh ecdh-key-agreement <private-key-name> <peer-public-key>"
    echo ""
    echo "Names are in string format. Size is an integer. Hash-digest and signature are base64-encoded sha256-sums. Peer-public-key is base64-encoded."
}

certificate_renew_message()
{
    request_id=$RANDOM
    certificate=$1
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "renew_certificate",
    "params" : {"certificate": "$certificate"}
}
EOF
}

set_certificates_list_message()
{
    request_id=$RANDOM
    certificates=$1
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "set_certificates_list",
    "params" : {"certificates": [$certificates]}
}
EOF
}

certificate_get_message()
{
    request_id=$RANDOM
    certificate=$1
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "get_certificate",
    "params" : {"certificate": "$certificate"}
}
EOF
}

public_key_get_message()
{
    request_id=$RANDOM
    public_key=$1
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "get_public_key",
    "params" : {"key": "$public_key"}
}
EOF
}

generate_random_message()
{
    request_id=$RANDOM
    size=$1
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "generate_random",
    "params" : {"size": $size}
}
EOF
}

asymmetric_sign_message()
{
    request_id=$RANDOM
    private_key_name=$1
    hash_digest=$2
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "asymmetric_sign",
    "params" : {"private_key_name": "$private_key_name",
                "hash_digest": "$hash_digest"}
}
EOF
}

asymmetric_verify_message()
{
    request_id=$RANDOM
    public_key_name=$1
    hash_digest=$2
    signature=$3
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "asymmetric_verify",
    "params" : {"public_key_name": "$public_key_name",
                "hash_digest": "$hash_digest",
                "signature": "$signature"}
}
EOF
}

ecdh_key_agreement_message()
{
    request_id=$RANDOM
    private_key_name=$1
    peer_public_key=$2
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "ecdh_key_agreement",
    "params" : {"private_key_name": "$private_key_name",
                "peer_public_key": "$peer_public_key"}
}
EOF
}

device_cert_renew_message()
{
    request_id=$RANDOM
    device_name=$1
    cert_name=$2
    csr=$3
    cat <<EOF
{
    "request_id": "$request_id",
    "method": "device_renew_certificate",
    "params" : {"device_name": "$device_name",
                "certificate_name": "$cert_name",
                "csr": "$csr"}
}
EOF
}

operation=$1

case $operation in
    set-certificates-list )  certificates=""
                             while [ "$2" != "" ]; do
                                 name=$2
                                 certificates=$certificates"\"$name\""
                                 shift
                                 if [ "$2" != "" ] ; then
                                     certificates=$certificates","
                                 fi
                             done
                             message="$(set_certificates_list_message $certificates)"
                             echo "$message"
                             mosquitto_pub -t MQTT -m "$message"
                             ;;
    renew-certificate )   message="$(certificate_renew_message $2)"
                          echo "$message"
                          mosquitto_pub -t MQTT -m "$message"
                          ;;
    get-certificate )     message="$(certificate_get_message $2)"
                          echo "$message"
                          mosquitto_pub -t MQTT -m "$message"
                          ;;
    get-public-key )      message="$(public_key_get_message $2)"
                          echo "$message"
                          mosquitto_pub -t MQTT -m "$message"
                          ;;
    generate-random )     message="$(generate_random_message $2)"
                          echo "$message"
                          mosquitto_pub -t MQTT -m "$message"
                          ;;
    asymmetric-sign )     message="$(asymmetric_sign_message $2 $3)"
                          echo "$message"
                          mosquitto_pub -t MQTT -m "$message"
                          ;;
    asymmetric-verify )   message="$(asymmetric_verify_message $2 $3 $4)"
                          echo "$message"
                          mosquitto_pub -t MQTT -m "$message"
                          ;;
    ecdh-key-agreement )  message="$(ecdh_key_agreement_message $2 $3)"
                          echo "$message"
                          mosquitto_pub -t MQTT -m "$message"
                          ;;
    device-cert-renew )   message="$(device_cert_renew_message $2 $3 $4)"
                          echo "$message"
                          mosquitto_pub -t MQTT -m "$message"
                          ;;
    * )                   usage
                          exit 1
esac
