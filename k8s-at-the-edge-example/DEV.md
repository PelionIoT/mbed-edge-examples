
# Local Development

This guide will help you set up and run the Virtual Device Server locally for development.

## Prerequisites

Make sure you have the following installed:
- [Docker](https://docs.docker.com/get-docker/) and Docker Compose
- [Rust](https://rustup.rs/) toolchain (for the backend)
- [Node.js](https://nodejs.org/) (v16 or later, for the frontend)
- [Git](https://git-scm.com/) (to clone the repository)

### Step 1: Clone and Setup

```bash
# Clone the repository
git clone <repository-url>
cd virtual-device-server

# Verify the structure
ls -la
# Should show: dummy-device-app/, web-portal/, k8s/, README.md
```

### Step 2: Start PostgreSQL Database

```bash
# Start PostgreSQL in Docker
docker run -d --name postgres-dev \
  -e POSTGRES_DB=device_db \
  -e POSTGRES_USER=postgres \
  -e POSTGRES_PASSWORD=password \
  -p 5432:5432 \
  postgres:15-alpine

# Verify the database is running
docker ps | grep postgres-dev

# Optional: Check database logs
docker logs postgres-dev
```

**Alternative: Use Docker Compose (recommended)**

Create a `docker-compose.yml` file in the root directory:

```yaml
version: '3.8'
services:
  postgres:
    image: postgres:15-alpine
    container_name: postgres-dev
    environment:
      POSTGRES_DB: device_db
      POSTGRES_USER: postgres
      POSTGRES_PASSWORD: password
    ports:
      - "5432:5432"
    volumes:
      - postgres_data:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U postgres"]
      interval: 10s
      timeout: 5s
      retries: 5

volumes:
  postgres_data:
```

Then run:
```bash
docker-compose up -d postgres
```

### Step 3: Run the Rust Backend

```bash
# Navigate to the backend directory
cd dummy-device-app

# Install Rust dependencies (if not already done)
cargo check

# Set environment variables
export DATABASE_URL="postgres://postgres:password@localhost:5432/device_db"
export RUST_LOG=info

# Run the application
cargo run
```

You should see output like:
```
2024-01-15T10:30:00.000Z INFO  Initializing dummy device server
2024-01-15T10:30:00.100Z INFO  Successfully connected to database
2024-01-15T10:30:00.200Z INFO  Database migrations completed successfully
2024-01-15T10:30:00.300Z INFO  No devices found, initializing default devices
2024-01-15T10:30:00.400Z INFO  Successfully initialized 4 default devices
2024-01-15T10:30:00.500Z INFO  Starting server on 0.0.0.0:3000
```

**Troubleshooting Backend:**
- If you get database connection errors, ensure PostgreSQL is running: `docker ps | grep postgres`
- If migrations fail, check the database logs: `docker logs postgres-dev`
- For more detailed logs, set `RUST_LOG=debug`

### Step 4: Run the React Frontend

Open a new terminal window and navigate to the frontend directory:

```bash
# Navigate to the frontend directory
cd web-portal

# Install Node.js dependencies
npm install

# Start the development server
npm start
```

You should see output like:
```
Compiled successfully!

You can now view web-portal in the browser.

  Local:            http://localhost:3001
  On Your Network:  http://192.168.1.100:3001

Note that the development build is not optimized.
To create a production build, use npm run build.
```

**Note:** The frontend runs on port 3001 to avoid conflicts with the backend (port 3000).

### Step 5: Access the Application

Once both services are running:

- **Web UI**: http://localhost:3001
- **Backend API**: http://localhost:3000
- **Health Check**: http://localhost:3000/health
- **API Documentation**: See the API Endpoints section below

### Step 6: Test the Setup

1. **Test the Health Check:**
```bash
curl http://localhost:3000/health
# Should return: {"success":true,"data":"OK","error":null}
```

2. **Test the API:**
```bash
# List all devices
curl http://localhost:3000/devices

# Create a new device
curl -X POST http://localhost:3000/devices \
  -H "Content-Type: application/json" \
  -d '{"name": "Test Light", "device_type": "LightBulb"}'
```

3. **Test the Web UI:**
- Open http://localhost:3001 in your browser
- You should see the device management interface
- Try creating, viewing, and updating devices

#### Development Workflow

1. **Backend Development:**
```bash
cd dummy-device-app
# Make changes to src/main.rs
cargo run  # Automatically recompiles and restarts
```

2. **Frontend Development:**
```bash
cd web-portal
# Make changes to src/App.js
# The development server automatically reloads
```

3. **Database Changes:**
```bash
cd dummy-device-app
# Create a new migration
sqlx migrate add <migration_name>
# Edit the generated migration file
cargo run  # Applies the migration
```

#### Stopping the Services

```bash
# Stop the backend (Ctrl+C in the terminal)
# Stop the frontend (Ctrl+C in the terminal)

# Stop PostgreSQL
docker stop postgres-dev
docker rm postgres-dev

# Or if using Docker Compose
docker-compose down
```

#### Environment Variables

Create a `.env` file in the `dummy-device-app` directory for easier development:

```bash
# dummy-device-app/.env
DATABASE_URL=postgres://postgres:password@localhost:5432/device_db
RUST_LOG=info
PORT=3000
```

Then you can run the backend with:
```bash
cd dummy-device-app
cargo run
```

#### Logging

The backend includes comprehensive logging. To see different log levels:

```bash
# Info level (default)
RUST_LOG=info cargo run

# Debug level (more verbose)
RUST_LOG=debug cargo run

# Error only
RUST_LOG=error cargo run
```

See `dummy-device-app/LOGGING.md` for detailed logging documentation.

### Adding New Device Types

1. Update the `DeviceType` enum in `dummy-device-app/src/main.rs`
2. Add the new type to the database migration
3. Update the default state logic in the `create_device` function
4. Add the device icon and control logic in `web-portal/src/App.js`

### Testing

```bash
# Test the Rust backend
cd dummy-device-app
cargo test

# Test the React frontend
cd web-portal
npm test
```

## API Endpoints

### Health Check
- `GET /health` - Service health status

### Devices
- `GET /devices` - List all devices
- `POST /devices` - Create a new device
- `GET /devices/{id}` - Get device details
- `PUT /devices/{id}/state` - Update device state

### Example API Usage

```bash
# List all devices
curl http://localhost:3000/devices

# Create a new light bulb
curl -X POST http://localhost:3000/devices \
  -H "Content-Type: application/json" \
  -d '{"name": "Living Room Light", "device_type": "LightBulb"}'

# Update device state
curl -X PUT http://localhost:3000/devices/{device-id}/state \
  -H "Content-Type: application/json" \
  -d '{"state": {"on": true}}'
```

## Features

- **Real-time Device Management**: View and control device states through a modern web interface
- **Persistent Storage**: All device states are stored in PostgreSQL
- **Scalable Architecture**: Microservices architecture with separate pods for each component
- **Health Monitoring**: Built-in health checks for all services
- **RESTful API**: Clean, documented API for device management
- **Responsive UI**: Modern React interface with Tailwind CSS

