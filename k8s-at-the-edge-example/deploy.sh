#!/bin/bash

set -e

echo "🚀 Deploying Virtual Device Server..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if kubectl is available
if ! command -v kubectl &> /dev/null; then
    print_error "kubectl is not installed or not in PATH"
    exit 1
fi

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    print_error "Docker is not installed or not in PATH"
    exit 1
fi

print_status "Building Docker images..."

# Build Rust application
print_status "Building dummy-device-app..."
cd dummy-device-app
docker build -t dummy-device-app:latest .
cd ..

# Build React application
print_status "Building web-portal..."
cd web-portal
docker build -t web-portal:latest .
cd ..

print_status "Deploying to Kubernetes..."

# Apply all Kubernetes manifests
kubectl apply -f k8s/

print_status "Waiting for pods to be ready..."

# Wait for pods to be ready
kubectl wait --for=condition=ready pod -l app=postgres -n virtual-device-server --timeout=300s
kubectl wait --for=condition=ready pod -l app=dummy-device-app -n virtual-device-server --timeout=300s
kubectl wait --for=condition=ready pod -l app=web-portal -n virtual-device-server --timeout=300s
kubectl wait --for=condition=ready pod -l app=nginx -n virtual-device-server --timeout=300s

print_status "Deployment completed successfully!"

# Get service information
print_status "Service information:"
kubectl get svc -n virtual-device-server

# Check if we're using minikube
if kubectl config current-context | grep -q "minikube"; then
    print_status "Opening service in browser (minikube)..."
    minikube service nginx -n virtual-device-server
else
    print_status "To access the application:"
    echo "1. Get the external IP: kubectl get svc nginx -n virtual-device-server"
    echo "2. Open http://<EXTERNAL-IP> in your browser"
fi

print_status "To view logs:"
echo "  Backend: kubectl logs -n virtual-device-server -l app=dummy-device-app"
echo "  Frontend: kubectl logs -n virtual-device-server -l app=web-portal"
echo "  Nginx: kubectl logs -n virtual-device-server -l app=nginx"

print_status "To delete the deployment:"
echo "  kubectl delete namespace virtual-device-server" 