use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::Json,
    routing::{get, post, put},
    Router,
    http::Request,
    middleware::{self, Next},
    response::Response,
};
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use sqlx::{postgres::PgPoolOptions, PgPool, Row};
use std::sync::Arc;
use tower_http::cors::{Any, CorsLayer};
use tracing::{info, warn, debug, error};
use uuid::Uuid;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub enum DeviceType {
    LightBulb,
    Switch,
    TemperatureSensor,
    HumiditySensor,
}

impl DeviceType {
    fn to_string(&self) -> &'static str {
        match self {
            DeviceType::LightBulb => "LightBulb",
            DeviceType::Switch => "Switch",
            DeviceType::TemperatureSensor => "TemperatureSensor",
            DeviceType::HumiditySensor => "HumiditySensor",
        }
    }
    
    fn from_string(s: &str) -> Option<Self> {
        match s {
            "LightBulb" => Some(DeviceType::LightBulb),
            "Switch" => Some(DeviceType::Switch),
            "TemperatureSensor" => Some(DeviceType::TemperatureSensor),
            "HumiditySensor" => Some(DeviceType::HumiditySensor),
            _ => None,
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Device {
    #[serde(with = "uuid::serde::simple")]
    pub id: Uuid,
    pub name: String,
    pub device_type: DeviceType,
    pub state: serde_json::Value,
    #[serde(with = "chrono::serde::ts_seconds")]
    pub created_at: DateTime<Utc>,
    #[serde(with = "chrono::serde::ts_seconds")]
    pub updated_at: DateTime<Utc>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CreateDeviceRequest {
    pub name: String,
    #[serde(alias = "deviceType", alias = "device_type")]
    pub device_type: DeviceType,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct UpdateDeviceStateRequest {
    pub state: serde_json::Value,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ApiResponse<T> {
    pub success: bool,
    pub data: Option<T>,
    pub error: Option<String>,
}

struct AppState {
    db: PgPool,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing
    tracing_subscriber::fmt::init();
    info!("Initializing dummy device server");

    // Database connection
    let database_url = std::env::var("DATABASE_URL")
        .unwrap_or_else(|_| "postgres://postgres:password@localhost:5432/device_db".to_string());
    
    debug!("Connecting to database at: {}", database_url.replace(":password@", ":****@"));
    
    let pool = PgPoolOptions::new()
        .max_connections(5)
        .connect(&database_url)
        .await?;

    info!("Successfully connected to database");

    // Run migrations
    debug!("Running database migrations");
    sqlx::migrate!("./migrations").run(&pool).await?;
    info!("Database migrations completed successfully");

    // Initialize with some default devices if none exist
    debug!("Checking for existing devices");
    initialize_default_devices(&pool).await?;

    let state = Arc::new(AppState { db: pool });

    // CORS configuration
    debug!("Configuring CORS");
    let cors = CorsLayer::new()
        .allow_origin(Any)
        .allow_methods(Any)
        .allow_headers(Any);

    // Build our application with a route
    debug!("Setting up application routes");
    let app = Router::new()
        .route("/health", get(health_check))
        .route("/devices", get(get_devices))
        .route("/devices", post(create_device))
        .route("/devices/:id", get(get_device))
        .route("/devices/:id/state", put(update_device_state))
        .layer(cors)
        .layer(middleware::from_fn(log_requests))
        .with_state(state);

    let port = std::env::var("PORT").unwrap_or_else(|_| "3000".to_string());
    let addr = format!("0.0.0.0:{}", port);
    
    info!("Starting server on {}", addr);
    debug!("Server configuration: port={}, max_connections=5", port);
    
    axum::Server::bind(&addr.parse()?)
        .serve(app.into_make_service())
        .await?;

    Ok(())
}

async fn log_requests<B>(req: Request<B>, next: Next<B>) -> Response {
    let method = req.method().clone();
    let uri = req.uri().clone();
    
    debug!("Request: {} {}", method, uri);
    
    // For POST requests to /devices, try to log the body
    if method == "POST" && uri.path() == "/devices" {
        debug!("POST request to /devices - this is where device creation happens");
    }
    
    let response = next.run(req).await;
    response
}

async fn health_check() -> Json<ApiResponse<String>> {
    debug!("Health check requested");
    info!("Health check endpoint called");
    
    Json(ApiResponse {
        success: true,
        data: Some("OK".to_string()),
        error: None,
    })
}

async fn get_devices(
    State(state): State<Arc<AppState>>,
) -> Result<Json<ApiResponse<Vec<Device>>>, StatusCode> {
    debug!("GET /devices - Fetching all devices");
    
    let rows = sqlx::query(
        r#"
        SELECT 
            id, name, device_type::text as device_type, state, created_at, updated_at
        FROM devices 
        ORDER BY created_at DESC
        "#
    )
    .fetch_all(&state.db)
    .await
    .map_err(|e| {
        error!("Failed to fetch devices from database: {}", e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?;

    debug!("Retrieved {} device rows from database", rows.len());

    let devices: Vec<Device> = rows
        .into_iter()
        .filter_map(|row| {
            let id: Uuid = row.try_get("id").ok()?;
            let name: String = row.try_get("name").ok()?;
            let device_type_str: String = row.try_get("device_type").ok()?;
            let state: serde_json::Value = row.try_get("state").ok()?;
            let created_at: DateTime<Utc> = row.try_get("created_at").ok()?;
            let updated_at: DateTime<Utc> = row.try_get("updated_at").ok()?;
            
            let device_type = DeviceType::from_string(&device_type_str)?;
            Some(Device {
                id,
                name,
                device_type,
                state,
                created_at,
                updated_at,
            })
        })
        .collect();

    info!("Successfully retrieved {} devices", devices.len());
    debug!("Device types: {:?}", devices.iter().map(|d| &d.device_type).collect::<Vec<_>>());

    Ok(Json(ApiResponse {
        success: true,
        data: Some(devices),
        error: None,
    }))
}

async fn get_device(
    State(state): State<Arc<AppState>>,
    Path(id): Path<Uuid>,
) -> Result<Json<ApiResponse<Device>>, StatusCode> {
    debug!("GET /devices/{} - Fetching specific device", id);
    
    let row = sqlx::query(
        r#"
        SELECT 
            id, name, device_type::text as device_type, state, created_at, updated_at
        FROM devices 
        WHERE id = $1
        "#
    )
    .bind(id)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| {
        error!("Failed to fetch device {} from database: {}", id, e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?
    .ok_or_else(|| {
        warn!("Device with id {} not found", id);
        StatusCode::NOT_FOUND
    })?;

    debug!("Found device row for id: {}", id);

    let id: Uuid = row.try_get("id").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let name: String = row.try_get("name").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let device_type_str: String = row.try_get("device_type").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let state: serde_json::Value = row.try_get("state").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let created_at: DateTime<Utc> = row.try_get("created_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let updated_at: DateTime<Utc> = row.try_get("updated_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    
    let device_type = DeviceType::from_string(&device_type_str)
        .ok_or(StatusCode::INTERNAL_SERVER_ERROR)?;

    let device = Device {
        id,
        name,
        device_type,
        state,
        created_at,
        updated_at,
    };

    info!("Successfully retrieved device: {} ({:?})", device.name, device.device_type);
    debug!("Device state: {:?}", device.state);

    Ok(Json(ApiResponse {
        success: true,
        data: Some(device),
        error: None,
    }))
}

async fn create_device(
    State(state): State<Arc<AppState>>,
    Json(payload): Json<CreateDeviceRequest>,
) -> Result<Json<ApiResponse<Device>>, StatusCode> {
    debug!("POST /devices - Creating new device: {} ({:?})", payload.name, payload.device_type);
    
    // Log the raw payload for debugging
    debug!("Raw payload: {:?}", payload);
    
    let id = Uuid::new_v4();
    let now = Utc::now();
    
    // Set default state based on device type
    let default_state = match payload.device_type {
        DeviceType::LightBulb => serde_json::json!({ "on": false }),
        DeviceType::Switch => serde_json::json!({ "on": false }),
        DeviceType::TemperatureSensor => serde_json::json!({ "temperature": 22.0 }),
        DeviceType::HumiditySensor => serde_json::json!({ "humidity": 50.0 }),
    };

    debug!("Generated device id: {}, default state: {:?}", id, default_state);

    let row = sqlx::query(
        r#"
        INSERT INTO devices (id, name, device_type, state, created_at, updated_at)
        VALUES ($1, $2, $3::device_type, $4, $5, $6)
        RETURNING 
            id, name, device_type::text as device_type, state, created_at, updated_at
        "#
    )
    .bind(id)
    .bind(&payload.name)
    .bind(payload.device_type.to_string())
    .bind(default_state)
    .bind(now)
    .bind(now)
    .fetch_one(&state.db)
    .await
    .map_err(|e| {
        error!("Failed to create device '{}' in database: {}", payload.name, e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?;

    debug!("Device inserted into database successfully");

    let id: Uuid = row.try_get("id").map_err(|e| {
        error!("Failed to get id from row: {}", e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?;
    let name: String = row.try_get("name").map_err(|e| {
        error!("Failed to get name from row: {}", e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?;
    // Get device_type as text (cast in SQL query)
    let device_type_str: String = row.try_get("device_type").map_err(|e| {
        error!("Failed to get device_type from row: {}", e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?;
    let state: serde_json::Value = row.try_get("state").map_err(|e| {
        error!("Failed to get state from row: {}", e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?;
    let created_at: DateTime<Utc> = row.try_get("created_at").map_err(|e| {
        error!("Failed to get created_at from row: {}", e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?;
    let updated_at: DateTime<Utc> = row.try_get("updated_at").map_err(|e| {
        error!("Failed to get updated_at from row: {}", e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?;
    
    let device_type = DeviceType::from_string(&device_type_str)
        .ok_or_else(|| {
            error!("Failed to parse device_type from string: {}", device_type_str);
            StatusCode::INTERNAL_SERVER_ERROR
        })?;

    debug!("Creating device struct with: id={}, name={}, device_type={:?}", id, name, device_type);

    let device = Device {
        id,
        name,
        device_type,
        state,
        created_at,
        updated_at,
    };

    debug!("Device struct created successfully: {:?}", device);

    info!("Successfully created device: {} ({:?}) with id: {}", device.name, device.device_type, device.id);
    debug!("Device details: created_at={}, updated_at={}, state={:?}", device.created_at, device.updated_at, device.state);

    let response = ApiResponse {
        success: true,
        data: Some(device),
        error: None,
    };
    
    debug!("Sending response: {:?}", response);
    
    // Try to serialize the response to see if there's an issue
    match serde_json::to_string(&response) {
        Ok(json_str) => {
            debug!("Response serialized successfully: {}", json_str);
            Ok(Json(response))
        }
        Err(e) => {
            error!("Failed to serialize response: {}", e);
            Err(StatusCode::INTERNAL_SERVER_ERROR)
        }
    }
}

async fn update_device_state(
    State(state): State<Arc<AppState>>,
    Path(id): Path<Uuid>,
    Json(payload): Json<UpdateDeviceStateRequest>,
) -> Result<Json<ApiResponse<Device>>, StatusCode> {
    debug!("PUT /devices/{}/state - Updating device state: {:?}", id, payload.state);
    debug!("Received UUID: {}", id);
    
    let now = Utc::now();

    let row = sqlx::query(
        r#"
        UPDATE devices 
        SET state = $1, updated_at = $2
        WHERE id = $3
        RETURNING 
            id, name, device_type::text as device_type, state, created_at, updated_at
        "#
    )
    .bind(payload.state)
    .bind(now)
    .bind(id)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| {
        error!("Failed to update device state for id {}: {}", id, e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?
    .ok_or_else(|| {
        warn!("Device with id {} not found for state update", id);
        StatusCode::NOT_FOUND
    })?;

    debug!("Device state updated in database for id: {}", id);

    let id: Uuid = row.try_get("id").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let name: String = row.try_get("name").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let device_type_str: String = row.try_get("device_type").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let state: serde_json::Value = row.try_get("state").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let created_at: DateTime<Utc> = row.try_get("created_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let updated_at: DateTime<Utc> = row.try_get("updated_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    
    let device_type = DeviceType::from_string(&device_type_str)
        .ok_or(StatusCode::INTERNAL_SERVER_ERROR)?;

    let device = Device {
        id,
        name,
        device_type,
        state,
        created_at,
        updated_at,
    };

    info!("Successfully updated device state: {} ({:?}) -> {:?}", device.name, device.device_type, device.state);
    debug!("Device update details: updated_at={}, previous_state_unknown", device.updated_at);

    Ok(Json(ApiResponse {
        success: true,
        data: Some(device),
        error: None,
    }))
}

async fn initialize_default_devices(pool: &PgPool) -> Result<(), sqlx::Error> {
    debug!("Checking device count in database");
    
    let count: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM devices")
        .fetch_one(pool)
        .await?;

    debug!("Found {} existing devices in database", count);

    if count == 0 {
        info!("No devices found, initializing default devices");
        
        let devices = vec![
            ("Living Room Light", DeviceType::LightBulb, serde_json::json!({ "on": false })),
            ("Kitchen Switch", DeviceType::Switch, serde_json::json!({ "on": true })),
            ("Bedroom Temperature", DeviceType::TemperatureSensor, serde_json::json!({ "temperature": 22.5 })),
            ("Office Humidity", DeviceType::HumiditySensor, serde_json::json!({ "humidity": 45.2 })),
        ];

        let device_count = devices.len();
        debug!("Creating {} default devices", device_count);

        for (name, device_type, state) in devices {
            let id = Uuid::new_v4();
            let now = Utc::now();
            
            debug!("Creating default device: {} ({:?}) with id: {}", name, device_type, id);
            
            sqlx::query(
                "INSERT INTO devices (id, name, device_type, state, created_at, updated_at) VALUES ($1, $2, $3::device_type, $4, $5, $6)"
            )
            .bind(id)
            .bind(name)
            .bind(device_type.to_string())
            .bind(state)
            .bind(now)
            .bind(now)
            .execute(pool)
            .await?;
            
            debug!("Successfully created default device: {} ({:?})", name, device_type);
        }
        
        info!("Successfully initialized {} default devices", device_count);
    } else {
        debug!("Skipping default device initialization - {} devices already exist", count);
    }

    Ok(())
} 