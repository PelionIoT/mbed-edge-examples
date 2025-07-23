#!/bin/bash

# Virtual Device Server - Local Development Setup Script
# This script sets up the local development environment

set -e  # Exit on any error

echo "🚀 Setting up Virtual Device Server for local development..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check prerequisites
check_prerequisites() {
    print_status "Checking prerequisites..."
    
    # Check if Docker is installed
    if ! command -v docker &> /dev/null; then
        print_error "Docker is not installed. Please install Docker first."
        exit 1
    fi
    
    # Check if Docker is running
    if ! docker info &> /dev/null; then
        print_error "Docker is not running. Please start Docker first."
        exit 1
    fi
    
    # Check if Rust is installed
    if ! command -v cargo &> /dev/null; then
        print_error "Rust is not installed. Please install Rust first: https://rustup.rs/"
        exit 1
    fi
    
    # Check if Node.js is installed
    if ! command -v node &> /dev/null; then
        print_error "Node.js is not installed. Please install Node.js first: https://nodejs.org/"
        exit 1
    fi
    
    print_success "All prerequisites are satisfied!"
}

# Start PostgreSQL
start_postgres() {
    print_status "Starting PostgreSQL database..."
    
    # Check if postgres container already exists
    if docker ps -a --format "table {{.Names}}" | grep -q "postgres-dev"; then
        print_warning "PostgreSQL container already exists. Stopping and removing..."
        docker stop postgres-dev 2>/dev/null || true
        docker rm postgres-dev 2>/dev/null || true
    fi
    
    # Start PostgreSQL using Docker Compose
    if [ -f "docker-compose.dev.yml" ]; then
        print_status "Using Docker Compose to start PostgreSQL..."
        docker-compose -f docker-compose.dev.yml up -d postgres
    else
        print_status "Using Docker run to start PostgreSQL..."
        docker run -d --name postgres-dev \
            -e POSTGRES_DB=device_db \
            -e POSTGRES_USER=postgres \
            -e POSTGRES_PASSWORD=password \
            -p 5432:5432 \
            postgres:15-alpine
    fi
    
    # Wait for PostgreSQL to be ready
    print_status "Waiting for PostgreSQL to be ready..."
    for i in {1..30}; do
        if docker exec postgres-dev pg_isready -U postgres &>/dev/null; then
            print_success "PostgreSQL is ready!"
            break
        fi
        if [ $i -eq 30 ]; then
            print_error "PostgreSQL failed to start within 30 seconds"
            exit 1
        fi
        sleep 1
    done
}

# Setup backend
setup_backend() {
    print_status "Setting up Rust backend..."
    
    cd dummy-device-app
    
    # Check if .env file exists, create if not
    if [ ! -f ".env" ]; then
        print_status "Creating .env file for backend..."
        cat > .env << EOF
DATABASE_URL=postgres://postgres:password@localhost:5432/device_db
RUST_LOG=info
PORT=3000
EOF
        print_success "Created .env file"
    fi
    
    # Install dependencies
    print_status "Installing Rust dependencies..."
    cargo check
    
    print_success "Backend setup complete!"
    cd ..
}

# Setup frontend
setup_frontend() {
    print_status "Setting up React frontend..."
    
    cd web-portal
    
    # Install Node.js dependencies
    print_status "Installing Node.js dependencies..."
    npm install
    
    print_success "Frontend setup complete!"
    cd ..
}

# Main setup function
main() {
    echo "=========================================="
    echo "Virtual Device Server - Local Setup"
    echo "=========================================="
    echo ""
    
    check_prerequisites
    start_postgres
    setup_backend
    setup_frontend
    
    echo ""
    echo "=========================================="
    print_success "Setup complete! 🎉"
    echo "=========================================="
    echo ""
    echo "Next steps:"
    echo "1. Start the backend:"
    echo "   cd dummy-device-app && cargo run"
    echo ""
    echo "2. Start the frontend (in a new terminal):"
    echo "   cd web-portal && npm start"
    echo ""
    echo "3. Access the application:"
    echo "   - Web UI: http://localhost:3001"
    echo "   - Backend API: http://localhost:3000"
    echo "   - Health Check: http://localhost:3000/health"
    echo ""
    echo "To stop PostgreSQL:"
    echo "   docker-compose down"
    echo "   or"
    echo "   docker stop postgres-dev && docker rm postgres-dev"
    echo ""
}

# Run main function
main "$@" 