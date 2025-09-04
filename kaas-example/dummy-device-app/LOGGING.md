# Logging Configuration

This application uses the `tracing` crate for comprehensive logging with different log levels.

## Log Levels

The application supports the following log levels:

- **ERROR**: Critical errors that prevent the application from functioning
- **WARN**: Warning messages for potential issues
- **INFO**: General information about application operations
- **DEBUG**: Detailed debugging information

## Log Messages Added

### Application Startup
- `info!("Initializing dummy device server")`
- `debug!("Connecting to database at: ...")`
- `info!("Successfully connected to database")`
- `debug!("Running database migrations")`
- `info!("Database migrations completed successfully")`
- `debug!("Checking for existing devices")`
- `debug!("Configuring CORS")`
- `debug!("Setting up application routes")`
- `info!("Starting server on ...")`
- `debug!("Server configuration: port=..., max_connections=5")`

### Health Check Endpoint
- `debug!("Health check requested")`
- `info!("Health check endpoint called")`

### Device Operations

#### Get All Devices
- `debug!("GET /devices - Fetching all devices")`
- `debug!("Retrieved X device rows from database")`
- `info!("Successfully retrieved X devices")`
- `debug!("Device types: [...]")`

#### Get Single Device
- `debug!("GET /devices/{id} - Fetching specific device")`
- `debug!("Found device row for id: {id}")`
- `info!("Successfully retrieved device: {name} ({type})")`
- `debug!("Device state: {...}")`

#### Create Device
- `debug!("POST /devices - Creating new device: {name} ({type})")`
- `debug!("Generated device id: {id}, default state: {...}")`
- `debug!("Device inserted into database successfully")`
- `info!("Successfully created device: {name} ({type}) with id: {id}")`
- `debug!("Device details: created_at=..., updated_at=..., state={...}")`

#### Update Device State
- `debug!("PUT /devices/{id}/state - Updating device state: {...}")`
- `debug!("Device state updated in database for id: {id}")`
- `info!("Successfully updated device state: {name} ({type}) -> {...}")`
- `debug!("Device update details: updated_at=..., previous_state_unknown")`

### Database Initialization
- `debug!("Checking device count in database")`
- `debug!("Found X existing devices in database")`
- `info!("No devices found, initializing default devices")`
- `debug!("Creating X default devices")`
- `debug!("Creating default device: {name} ({type}) with id: {id}")`
- `debug!("Successfully created default device: {name} ({type})")`
- `info!("Successfully initialized X default devices")`
- `debug!("Skipping default device initialization - X devices already exist")`

## Error Logging

The application logs errors with detailed context:
- Database connection failures
- Query execution errors
- Device not found errors
- Data parsing errors

## Setting Log Level

To control the log level, set the `RUST_LOG` environment variable:

```bash
# Show only info and above
RUST_LOG=info cargo run

# Show debug and above (most verbose)
RUST_LOG=debug cargo run

# Show only errors
RUST_LOG=error cargo run

# Show specific module logs
RUST_LOG=dummy_device_app=debug cargo run
```

## Example Output

When running with `RUST_LOG=info`:

```
2024-01-15T10:30:00.000Z INFO  Initializing dummy device server
2024-01-15T10:30:00.100Z INFO  Successfully connected to database
2024-01-15T10:30:00.200Z INFO  Database migrations completed successfully
2024-01-15T10:30:00.300Z INFO  No devices found, initializing default devices
2024-01-15T10:30:00.400Z INFO  Successfully initialized 4 default devices
2024-01-15T10:30:00.500Z INFO  Starting server on 0.0.0.0:3000
```

When running with `RUST_LOG=debug`, you'll see much more detailed information about each operation. 