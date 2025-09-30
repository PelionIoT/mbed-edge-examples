use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::Json,
    routing::{get, post, put, delete},
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
// WebSocket imports for future Protocol Translator implementation
use tokio_tungstenite::{client_async, tungstenite::Message};
use futures_util::{SinkExt, StreamExt};
use tokio::net::UnixStream;
use std::collections::HashMap;
use std::sync::Mutex;
use serde_json::json;
use tokio_tungstenite::tungstenite::handshake::client::generate_key;
use base64::{Engine as _, engine::general_purpose};
use tokio::sync::mpsc;

// LwM2M/Edge Core operation bit flags
const OP_READ: u8 = 0x01;
const OP_WRITE: u8 = 0x02;
const OP_EXECUTE: u8 = 0x04;
const OP_DELETE: u8 = 0x08;

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

// LwM2M Object and Resource IDs based on IPSO Alliance specifications
#[derive(Debug, Clone, Copy)]
pub enum LwM2MObjectType {
    DigitalOutput = 3201,
    TemperatureSensor = 3303,
    HumiditySensor = 3304,
    SetPoint = 3308,
    LightControl = 3311,
}

#[derive(Debug, Clone, Copy)]
pub enum LwM2MResource {
    DigitalInputState = 5500,
    SensorValue = 5700,
    SensorUnits = 5701,
    OnOffValue = 5850,
    SetPointValue = 5900,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LwM2MResourceData {
    pub resource_id: u16,
    pub operations: u8,
    #[serde(rename = "type")]
    pub resource_type: String,
    pub value: String, // Base64 encoded
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LwM2MObjectInstance {
    pub object_instance_id: u16,
    pub resources: Vec<LwM2MResourceData>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LwM2MObjectData {
    pub object_id: u16,
    pub object_instances: Vec<LwM2MObjectInstance>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceRegistrationParams {
    pub device_id: String,
    pub objects: Vec<LwM2MObjectData>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct JsonRpcRequest {
    pub jsonrpc: String,
    pub method: String,
    pub params: serde_json::Value,
    pub id: u64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct JsonRpcResponse {
    pub jsonrpc: String,
    pub result: Option<serde_json::Value>,
    pub error: Option<serde_json::Value>,
    pub id: u64,
}

#[derive(Clone, Debug)]
struct WriteEvent {
    device_id: String,
    object_id: u16,
    object_instance_id: u16,
    resource_id: u16,
    value_base64: String,
    operation: u8,
}

pub struct ProtocolTranslator {
    name: String,
    socket_path: String,
    api_path: String,
    request_id: u64,
    write: Option<Arc<tokio::sync::Mutex<futures_util::stream::SplitSink<tokio_tungstenite::WebSocketStream<UnixStream>, Message>>>>, 
    message_id: Arc<Mutex<u64>>,
    pending_requests: Arc<Mutex<HashMap<u64, tokio::sync::oneshot::Sender<serde_json::Value>>>>,
    write_events: Option<mpsc::Sender<WriteEvent>>,
}

impl ProtocolTranslator {
    pub fn new(name: String) -> Self {
        Self {
            name,
            socket_path: "/tmp/edge.sock".to_string(),
            api_path: "/1/pt".to_string(),
            request_id: 1,
            write: None,
            message_id: Arc::new(Mutex::new(1)),
            pending_requests: Arc::new(Mutex::new(HashMap::new())),
            write_events: None,
        }
    }

    pub fn set_write_events_sender(&mut self, sender: mpsc::Sender<WriteEvent>) {
        self.write_events = Some(sender);
    }

    pub async fn connect_and_register(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        let connect_path = self.socket_path.clone();
        let request_uri = format!("ws://localhost{}", self.api_path);
        debug!("Connecting to Protocol Translator over UDS: {} -> {}", connect_path, request_uri);

        // Connect to Unix Domain Socket
        let stream = UnixStream::connect(connect_path).await?;

        // Perform WebSocket client handshake over the UnixStream
        let request = axum::http::Request::builder()
            .method("GET")
            .uri(&request_uri)
            .header("Host", "localhost")
            .header("Connection", "Upgrade")
            .header("Upgrade", "websocket")
            .header("Sec-WebSocket-Version", "13")
            .header("Sec-WebSocket-Key", generate_key())
            .body(())?;

        let (ws_stream, _response) = client_async(request, stream).await?;
        let (write, mut read) = ws_stream.split();
        let write_arc = Arc::new(tokio::sync::Mutex::new(write));
        self.write = Some(write_arc.clone());

        // Spawn reader task to handle incoming messages and route responses
        let pending_map = self.pending_requests.clone();
        let write_events_tx = self.write_events.clone();
        tokio::spawn(async move {
            let write_for_calls = write_arc.clone();
            loop {
                match read.next().await {
                    Some(Ok(Message::Text(text))) => {
                        // { 
                        // "id":"2",
                        // "jsonrpc":"2.0",
                        // "method":"write",
                        // "params":{
                        //     "operation":2,
                        //     "uri":{
                        //         "deviceId":"Kitchen_Switch",
                        //         "objectId":3311,
                        //         "objectInstanceId":0,
                        //         "resourceId":5850
                        //     },
                        //     "value":"AQ=="
                        // }
                        // }
                        debug!("PT received: {}", text);
                        if let Ok(value) = serde_json::from_str::<serde_json::Value>(&text) {
                            if let Some(method) = value.get("method").and_then(|m| m.as_str()) {
                                debug!("Incoming call: {}", method);
                                if method == "write" {
                                    // respond with { result: "ok" } using the same id type (string/number)
                                    if let Some(id_field) = value.get("id") {
                                        let response = json!({
                                            "jsonrpc": "2.0",
                                            "id": id_field.clone(),
                                            "result": "ok",
                                        });
                                        debug!("Responding to write with ok; id={}", id_field);
                                        let mut writer = write_for_calls.lock().await;
                                        let _ = writer.send(Message::Text(response.to_string())).await;
                                    } else {
                                        debug!("Incoming write missing id; skipping response");
                                    }

                                    // Forward event to application for local state update
                                    if let (Some(params), Some(tx)) = (value.get("params"), write_events_tx.as_ref()) {
                                        let uri = params.get("uri");
                                        let value_b64 = params.get("value");
                                        let op = params.get("operation");
                                        if let (Some(uri), Some(value_b64), Some(op)) = (uri, value_b64, op) {
                                            let event = WriteEvent {
                                                device_id: uri.get("deviceId").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                                                object_id: uri.get("objectId").and_then(|v| v.as_u64()).unwrap_or(0) as u16,
                                                object_instance_id: uri.get("objectInstanceId").and_then(|v| v.as_u64()).unwrap_or(0) as u16,
                                                resource_id: uri.get("resourceId").and_then(|v| v.as_u64()).unwrap_or(0) as u16,
                                                value_base64: value_b64.as_str().unwrap_or("").to_string(),
                                                operation: op.as_u64().unwrap_or(0) as u8,
                                            };
                                            let _ = tx.send(event).await;
                                        }
                                    }
                                }
                            } else if let Some(id) = value.get("id").and_then(|v| v.as_u64()) {
                                let tx_opt = {
                                    let mut pending = pending_map.lock().unwrap();
                                    pending.remove(&id)
                                };
                                if let Some(tx) = tx_opt {
                                    let _ = tx.send(value);
                                } else {
                                    debug!("No pending requester for id {}", id);
                                }
                            }
                        }
                    }
                    Some(Ok(other)) => {
                        debug!("PT received non-text message: {:?}", other);
                    }
                    Some(Err(e)) => {
                        error!("PT socket read error: {}", e);
                        // Break and let caller recreate connection
                        break;
                    }
                    None => {
                        debug!("PT socket closed by peer");
                        break;
                    }
                }
            }
            // Optionally we could signal a reconnect here using a channel
        });

        // Register the protocol translator identity
        let params = json!({ "name": self.name });
        let _ = self.send_request("protocol_translator_register", params).await?;

        info!("Protocol Translator '{}' connected and registered with Edge Core", self.name);
        Ok(())
    }

    pub async fn register_device(&mut self, device: &Device) -> Result<(), Box<dyn std::error::Error>> {
        let device_params = self.create_device_params(device);

        debug!("Registering device with Edge Core (JSON-RPC)");
        let params_value = serde_json::to_value(&device_params)?;
        let _resp = self.send_request("device_register", params_value).await?;
        info!("Device '{}' ({:?}) registered with Edge Core", device.name, device.device_type);
        Ok(())
    }

    pub async fn write_device_state(&mut self, device: &Device) -> Result<(), Box<dyn std::error::Error>> {
        // Reuse the same payload structure (deviceId + objects)
        let params = self.create_device_params(device);
        let params_value = serde_json::to_value(&params)?;
        // Log the exact JSON we will send
        if let Ok(pretty) = serde_json::to_string_pretty(&params_value) {
            debug!("Sending device state update to Edge Core (write): {}", pretty);
        } else {
            debug!("Sending device state update to Edge Core (write)");
        }
        let _ = self.send_request("write", params_value).await?;
        Ok(())
    }

    pub async fn unregister_device(&mut self, device: &Device) -> Result<(), Box<dyn std::error::Error>> {
        let device_id: String = device.name.chars().map(|c| if c.is_alphanumeric() { c } else { '_' }).collect();
        let params = json!({
            "deviceId": device_id,
        });
        debug!("Unregistering device from Edge Core (device_unregister): {}", device.name);
        let _ = self.send_request("device_unregister", params).await?;
        Ok(())
    }

    fn create_device_params(&self, device: &Device) -> DeviceRegistrationParams {
        let (object_id, resources) = self.map_device_type_to_lwm2m(&device.device_type, &device.state);
        
        let resource_data = resources.into_iter().map(|(resource_id, operations, resource_type, value)| {
            LwM2MResourceData {
                resource_id,
                operations,
                resource_type,
                value: self.encode_value(&value),
            }
        }).collect();

        let object_instance = LwM2MObjectInstance {
            object_instance_id: 0,
            resources: resource_data,
        };

        let lwm2m_object = LwM2MObjectData {
            object_id,
            object_instances: vec![object_instance],
        };

        let params = DeviceRegistrationParams {
            device_id: device.name.chars().map(|c| if c.is_alphanumeric() { c } else { '_' }).collect::<String>(),
            objects: vec![lwm2m_object],
        };

        // Extra debug: print the derived on/off value if present
        if let Some(obj) = params.objects.first() {
            if obj.object_id == LwM2MObjectType::LightControl as u16 {
                if let Some(res) = obj.object_instances.first().and_then(|oi| oi.resources.iter().find(|r| r.resource_id == LwM2MResource::OnOffValue as u16)) {
                    debug!("Derived /{}/0/{} value (base64): {}", obj.object_id, res.resource_id, res.value);
                }
            }
        }

        params
    }

    fn map_device_type_to_lwm2m(&self, device_type: &DeviceType, state: &serde_json::Value) -> (u16, Vec<(u16, u8, String, serde_json::Value)>) {
        match device_type {
            DeviceType::LightBulb => {
                let on_off_value = state.get("on").unwrap_or(&serde_json::Value::Bool(false));
                (LwM2MObjectType::LightControl as u16, vec![
                    // Register On/Off with READ | WRITE. Some stacks expect "boolean" not "bool".
                    (LwM2MResource::OnOffValue as u16, OP_READ | OP_WRITE, "boolean".to_string(), on_off_value.clone()),
                ])
            },
            DeviceType::Switch => {
                let on_off_value = state.get("on").unwrap_or(&serde_json::Value::Bool(false));
                (LwM2MObjectType::LightControl as u16, vec![
                    (LwM2MResource::OnOffValue as u16, OP_READ | OP_WRITE, "boolean".to_string(), on_off_value.clone()),
                ])
            },
            DeviceType::TemperatureSensor => {
                let default_temp = serde_json::Value::Number(serde_json::Number::from_f64(22.0).unwrap());
                let temp_value = state.get("temperature").unwrap_or(&default_temp);
                (LwM2MObjectType::TemperatureSensor as u16, vec![
                    (LwM2MResource::SensorValue as u16, OP_READ, "float".to_string(), temp_value.clone()),
                    (LwM2MResource::SensorUnits as u16, OP_READ, "string".to_string(), serde_json::Value::String("Celsius".to_string())),
                ])
            },
            DeviceType::HumiditySensor => {
                let default_humidity = serde_json::Value::Number(serde_json::Number::from_f64(50.0).unwrap());
                let humidity_value = state.get("humidity").unwrap_or(&default_humidity);
                (LwM2MObjectType::HumiditySensor as u16, vec![
                    (LwM2MResource::SensorValue as u16, OP_READ, "float".to_string(), humidity_value.clone()),
                    (LwM2MResource::SensorUnits as u16, OP_READ, "string".to_string(), serde_json::Value::String("%".to_string())),
                ])
            },
        }
    }

    fn encode_value(&self, value: &serde_json::Value) -> String {
        match value {
            serde_json::Value::Bool(b) => {
                // Some stacks expect boolean as CBOR-like single byte, others as 8-byte int. We keep 1-byte for write/update.
                let bytes = if *b { vec![1u8] } else { vec![0u8] };
                general_purpose::STANDARD.encode(&bytes)
            },
            serde_json::Value::Number(n) => {
                if let Some(f) = n.as_f64() {
                    let mut bytes = vec![0u8; 8];
                    bytes.copy_from_slice(&f.to_be_bytes());
                    general_purpose::STANDARD.encode(&bytes)
                } else if let Some(i) = n.as_i64() {
                    let mut bytes = vec![0u8; 8];
                    bytes.copy_from_slice(&i.to_be_bytes());
                    general_purpose::STANDARD.encode(&bytes)
                } else {
                    general_purpose::STANDARD.encode(value.to_string().as_bytes())
                }
            },
            serde_json::Value::String(s) => {
                general_purpose::STANDARD.encode(s.as_bytes())
            },
            _ => {
                general_purpose::STANDARD.encode(value.to_string().as_bytes())
            }
        }
    }

    async fn send_request(&mut self, method: &str, params: serde_json::Value) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
        let id = {
            let mut guard = self.message_id.lock().unwrap();
            *guard += 1;
            *guard
        };

        let request = json!({
            "jsonrpc": "2.0",
            "id": id,
            "method": method,
            "params": params,
        });

        // Log the outgoing JSON-RPC request
        if let Ok(pretty) = serde_json::to_string_pretty(&request) {
            debug!("PT sending: {}", pretty);
        }

        let (tx, rx) = tokio::sync::oneshot::channel();
        {
            let mut pending = self.pending_requests.lock().unwrap();
            pending.insert(id, tx);
        }

        if let Some(write_arc) = &self.write {
            let mut writer = write_arc.lock().await;
            writer.send(Message::Text(request.to_string())).await?;
        } else {
            return Err("WebSocket not connected".into());
        }

        let response = rx.await?;
        Ok(response)
    }
}

// Simple reconnect loop that can be used by callers
async fn ensure_pt_connected(pt: &mut ProtocolTranslator) {
    let mut backoff_ms: u64 = 500;
    loop {
        match pt.connect_and_register().await {
            Ok(_) => return,
            Err(e) => {
                error!("Failed to connect/register PT: {}. Retrying in {}ms", e, backoff_ms);
                tokio::time::sleep(std::time::Duration::from_millis(backoff_ms)).await;
                backoff_ms = (backoff_ms * 2).min(10_000);
            }
        }
    }
}

struct AppState {
    db: PgPool,
    protocol_translator: Arc<tokio::sync::Mutex<ProtocolTranslator>>,
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

    // Initialize Protocol Translator
    debug!("Initializing Protocol Translator");
    let mut protocol_translator = ProtocolTranslator::new("dummy-device-app".to_string());
    // Channel for write events from PT -> app
    let (write_tx, mut write_rx) = mpsc::channel::<WriteEvent>(32);
    protocol_translator.set_write_events_sender(write_tx);
    // Use the new ensure_pt_connected function to handle reconnection
    ensure_pt_connected(&mut protocol_translator).await;
    info!("Protocol Translator initialized and connected to Edge Core");

    // Initialize with some default devices if none exist
    debug!("Checking for existing devices");
    initialize_default_devices(&pool, &mut protocol_translator).await?;

    let state = Arc::new(AppState { 
        db: pool,
        protocol_translator: Arc::new(tokio::sync::Mutex::new(protocol_translator)),
    });

    // Spawn task to consume write events and update local DB state
    {
        let state = state.clone();
        tokio::spawn(async move {
            while let Some(event) = write_rx.recv().await {
                // event -> { 
                // device_id: "Kitchen_Switch", 
                // object_id: 3311, 
                // object_instance_id: 0, 
                // resource_id: 5850, 
                // value_base64: "AQ==", 
                // operation: 2
                // }
                debug!("Applying PT write to local state: {:?}", event);
                if event.object_id == LwM2MObjectType::LightControl as u16 && event.resource_id == LwM2MResource::OnOffValue as u16 {
                    // Decode base64 1-byte boolean
                    if let Ok(bytes) = general_purpose::STANDARD.decode(&event.value_base64) {
                        let new_on = bytes.get(0).copied().unwrap_or(0) != 0;
                        // Update by device name (deviceId). Fallback to unsanitized (underscores -> spaces) if no match
                        let name_exact = event.device_id.clone();
                        let name_spaces = name_exact.replace('_', " ");
                        match sqlx::query(
                            r#"UPDATE devices SET state = $1, updated_at = $2 WHERE name = $3 OR name = $4"#
                        )
                        .bind(serde_json::json!({"on": new_on}))
                        .bind(Utc::now())
                        .bind(&name_exact)
                        .bind(&name_spaces)
                        .execute(&state.db)
                        .await {
                            Ok(res) => {
                                if res.rows_affected() == 0 {
                                    warn!("PT write applied to 0 rows for deviceId='{}' (also tried '{}')", name_exact, name_spaces);
                                } else {
                                    info!("Updated local device '{}' on={} from PT write (rows={})", name_exact, new_on, res.rows_affected());
                                }
                            }
                            Err(e) => {
                                error!("Failed to apply PT write to DB for {}: {}", name_exact, e);
                            }
                        }
                    }
                }
            }
        });
    }

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
        .route("/devices/:id", delete(delete_device))
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
    let device_state: serde_json::Value = row.try_get("state").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let created_at: DateTime<Utc> = row.try_get("created_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let updated_at: DateTime<Utc> = row.try_get("updated_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    
    let device_type = DeviceType::from_string(&device_type_str)
        .ok_or(StatusCode::INTERNAL_SERVER_ERROR)?;

    let device = Device {
        id,
        name,
        device_type,
        state: device_state,
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
    let device_state: serde_json::Value = row.try_get("state").map_err(|e| {
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
        state: device_state,
        created_at,
        updated_at,
    };

    debug!("Device struct created successfully: {:?}", device);

    info!("Successfully created device: {} ({:?}) with id: {}", device.name, device.device_type, device.id);
    debug!("Device details: created_at={}, updated_at={}, state={:?}", device.created_at, device.updated_at, device.state);

    // Register device with Protocol Translator (Edge Core)
    debug!("Registering device with Protocol Translator");
    {
        let mut pt = state.protocol_translator.lock().await;
        if let Err(e) = pt.register_device(&device).await {
            error!("Failed to register device with Protocol Translator: {}", e);
            // Continue anyway - device is created in database
        }
    }

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

async fn delete_device(
    State(state): State<Arc<AppState>>,
    Path(id): Path<Uuid>,
) -> Result<Json<ApiResponse<String>>, StatusCode> {
    debug!("DELETE /devices/{} - Deleting device", id);

    // Fetch device for PT unregister (needs name and type)
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
        error!("Failed to fetch device {} before delete: {}", id, e);
        StatusCode::INTERNAL_SERVER_ERROR
    })?
    .ok_or(StatusCode::NOT_FOUND)?;

    let name: String = row.try_get("name").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let device_type_str: String = row.try_get("device_type").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let device_state: serde_json::Value = row.try_get("state").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let created_at: DateTime<Utc> = row.try_get("created_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let updated_at: DateTime<Utc> = row.try_get("updated_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;

    let device_type = DeviceType::from_string(&device_type_str).ok_or(StatusCode::INTERNAL_SERVER_ERROR)?;
    let device = Device { id, name: name.clone(), device_type, state: device_state, created_at, updated_at };

    // Unregister from PT first (best effort)
    {
        let mut pt = state.protocol_translator.lock().await;
        if let Err(e) = pt.unregister_device(&device).await {
            warn!("Failed to unregister device '{}' from PT: {}", device.name, e);
        } else {
            debug!("Unregistered device '{}' from PT", device.name);
        }
    }

    // Delete from database
    let res = sqlx::query("DELETE FROM devices WHERE id = $1")
        .bind(id)
        .execute(&state.db)
        .await
        .map_err(|e| {
            error!("Failed to delete device {}: {}", id, e);
            StatusCode::INTERNAL_SERVER_ERROR
        })?;

    if res.rows_affected() == 0 {
        return Err(StatusCode::NOT_FOUND);
    }

    info!("Deleted device '{}' ({})", name, id);
    Ok(Json(ApiResponse { success: true, data: Some("deleted".to_string()), error: None }))
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
    let device_state: serde_json::Value = row.try_get("state").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let created_at: DateTime<Utc> = row.try_get("created_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    let updated_at: DateTime<Utc> = row.try_get("updated_at").map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    
    let device_type = DeviceType::from_string(&device_type_str)
        .ok_or(StatusCode::INTERNAL_SERVER_ERROR)?;

    let device = Device {
        id,
        name,
        device_type,
        state: device_state,
        created_at,
        updated_at,
    };

    info!("Successfully updated device state: {} ({:?}) -> {:?}", device.name, device.device_type, device.state);
    debug!("Device update details: updated_at={}, previous_state_unknown", device.updated_at);

    // Send the state update to Edge Core via Protocol Translator
    {
        let mut pt = state.protocol_translator.lock().await;
        if let Err(e) = pt.write_device_state(&device).await {
            error!("Failed to send device state update to Edge Core: {}", e);
        } else {
            debug!("Device state update forwarded to Edge Core via PT");
        }
    }

    Ok(Json(ApiResponse {
        success: true,
        data: Some(device),
        error: None,
    }))
}

async fn initialize_default_devices(pool: &PgPool, protocol_translator: &mut ProtocolTranslator) -> Result<(), Box<dyn std::error::Error>> {
    debug!("Checking device count in database");
    
    let count: i64 = sqlx::query_scalar("SELECT COUNT(*) FROM devices")
        .fetch_one(pool)
        .await?;

    debug!("Found {} existing devices in database", count);

    if count == 0 {
        info!("No devices found, initializing default devices");
        
        let devices = vec![
            ("Canopy Light", DeviceType::LightBulb, serde_json::json!({ "on": false })),
            ("Wall Pack Light", DeviceType::LightBulb, serde_json::json!({ "on": true })),
            ("Walk-in Cooler Temperature", DeviceType::TemperatureSensor, serde_json::json!({ "temperature": 22.5 })),
            ("Ambient Humidity Sensor", DeviceType::HumiditySensor, serde_json::json!({ "humidity": 45.2 })),
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
            .bind(state.clone())
            .bind(now)
            .bind(now)
            .execute(pool)
            .await?;
            
            debug!("Successfully created default device: {} ({:?})", name, device_type);
            
            // Create a temporary device struct for Protocol Translator registration
            let temp_device = Device {
                id,
                name: name.to_string(),
                device_type: device_type.clone(),
                state: state.clone(),
                created_at: now,
                updated_at: now,
            };
            
            // Register with Protocol Translator
            if let Err(e) = protocol_translator.register_device(&temp_device).await {
                error!("Failed to register default device '{}' with Protocol Translator: {}", name, e);
            } else {
                debug!("Successfully registered default device '{}' with Protocol Translator", name);
            }
        }
        
        info!("Successfully initialized {} default devices", device_count);
    } else {
        debug!("{} devices already exist - re-registering with Protocol Translator and publishing last known state", count);

        let rows = sqlx::query(
            r#"
            SELECT 
                id, name, device_type::text as device_type, state, created_at, updated_at
            FROM devices 
            ORDER BY created_at ASC
            "#
        )
        .fetch_all(pool)
        .await?;

        for row in rows {
            let id: Uuid = row.try_get("id")?;
            let name: String = row.try_get("name")?;
            let device_type_str: String = row.try_get("device_type")?;
            let device_state: serde_json::Value = row.try_get("state")?;
            let created_at: DateTime<Utc> = row.try_get("created_at")?;
            let updated_at: DateTime<Utc> = row.try_get("updated_at")?;

            if let Some(device_type) = DeviceType::from_string(&device_type_str) {
                let device = Device {
                    id,
                    name: name.clone(),
                    device_type,
                    state: device_state,
                    created_at,
                    updated_at,
                };

                // Register and then publish last known state
                if let Err(e) = protocol_translator.register_device(&device).await {
                    error!("Re-register device '{}' with PT failed: {}", device.name, e);
                } else {
                    debug!("Re-registered '{}' with PT", device.name);
                    if let Err(e) = protocol_translator.write_device_state(&device).await {
                        error!("Publish last state for '{}' failed: {}", device.name, e);
                    } else {
                        debug!("Published last known state for '{}' via PT", device.name);
                    }
                }
            } else {
                warn!("Skipping device '{}' with unknown device_type '{}'", name, device_type_str);
            }
        }
    }

    Ok(())
} 