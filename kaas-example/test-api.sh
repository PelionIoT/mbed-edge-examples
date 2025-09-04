#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

API_BASE_URL=${1:-"http://localhost:3000"}

print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

echo "🧪 Testing Virtual Device Server API at $API_BASE_URL"

# Test health endpoint
print_status "Testing health endpoint..."
if curl -s "$API_BASE_URL/health" | grep -q "OK"; then
    print_status "✅ Health check passed"
else
    print_error "❌ Health check failed"
    exit 1
fi

# Test getting devices
print_status "Testing get devices endpoint..."
DEVICES_RESPONSE=$(curl -s "$API_BASE_URL/devices")
if echo "$DEVICES_RESPONSE" | grep -q "success.*true"; then
    print_status "✅ Get devices endpoint working"
    DEVICE_COUNT=$(echo "$DEVICES_RESPONSE" | jq '.data | length' 2>/dev/null || echo "0")
    print_status "Found $DEVICE_COUNT devices"
else
    print_error "❌ Get devices endpoint failed"
    echo "Response: $DEVICES_RESPONSE"
fi

# Test creating a new device
print_status "Testing create device endpoint..."
CREATE_RESPONSE=$(curl -s -X POST "$API_BASE_URL/devices" \
    -H "Content-Type: application/json" \
    -d '{"name": "Test Light", "device_type": "LightBulb"}')

if echo "$CREATE_RESPONSE" | grep -q "success.*true"; then
    print_status "✅ Create device endpoint working"
    DEVICE_ID=$(echo "$CREATE_RESPONSE" | jq -r '.data.id' 2>/dev/null)
    if [ "$DEVICE_ID" != "null" ] && [ "$DEVICE_ID" != "" ]; then
        print_status "Created device with ID: $DEVICE_ID"
        
        # Test updating device state
        print_status "Testing update device state endpoint..."
        UPDATE_RESPONSE=$(curl -s -X PUT "$API_BASE_URL/devices/$DEVICE_ID/state" \
            -H "Content-Type: application/json" \
            -d '{"state": {"on": true}}')
        
        if echo "$UPDATE_RESPONSE" | grep -q "success.*true"; then
            print_status "✅ Update device state endpoint working"
        else
            print_error "❌ Update device state endpoint failed"
            echo "Response: $UPDATE_RESPONSE"
        fi
        
        # Test getting specific device
        print_status "Testing get specific device endpoint..."
        GET_DEVICE_RESPONSE=$(curl -s "$API_BASE_URL/devices/$DEVICE_ID")
        if echo "$GET_DEVICE_RESPONSE" | grep -q "success.*true"; then
            print_status "✅ Get specific device endpoint working"
        else
            print_error "❌ Get specific device endpoint failed"
            echo "Response: $GET_DEVICE_RESPONSE"
        fi
    else
        print_warning "⚠️ Could not extract device ID from response"
    fi
else
    print_error "❌ Create device endpoint failed"
    echo "Response: $CREATE_RESPONSE"
fi

print_status "🎉 API testing completed!" 