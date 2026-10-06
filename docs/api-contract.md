# TrueSense Backend API Contract Specification

This document defines the RESTful HTTP API contract between TrueSense ESP32 nodes / web dashboards and the FastAPI backend service.

---

## 1. General API Standards

- **Base URL**: `http://<server-ip>:8000/api/v1`
- **Content-Type**: `application/json`
- **Error Format**: All errors return a JSON payload with a `detail` field and appropriate HTTP status codes ($4xx$, $5xx$).

```json
{
  "detail": "Descriptive error message"
}
```

---

## 2. Endpoints

### 2.1 Device Registration
Enrolls a new physical node into the system.

- **HTTP Method**: `POST`
- **Endpoint**: `/devices/register`
- **Request Body**:
```json
{
  "device_id": "TS001",
  "name": "Factory Exhaust Node 1",
  "enrolled_fingerprint": [1.0, 0.98, 0.95, 0.92, 0.89, 0.87, 0.85, 0.84, 0.83, 0.82, 0.81, 0.80, 0.80, 0.79, 0.79, 0.78, 0.78, 0.78, 0.77, 0.77]
}
```
- **Response** (`201 Created`):
```json
{
  "status": "success",
  "device_id": "TS001",
  "message": "Device registered successfully"
}
```

---

### 2.2 Ingest Reading Batch
Ingests a batch of records uploaded by the ESP32 node during WiFi synchronization.

- **HTTP Method**: `POST`
- **Endpoint**: `/readings/ingest`
- **Request Body**:
```json
{
  "device_id": "TS001",
  "readings": [
    {
      "device_id": "TS001",
      "timestamp": 1750000000,
      "gas_ppm": 42.381,
      "power_mW": 823.420,
      "plausibility": "NORMAL",
      "fingerprint_status": "SENSOR_OK",
      "previous_hash": "0000000000000000000000000000000000000000000000000000000000000000",
      "hash": "2e8e7995bb9576188874c028488bafb8a5973133134cbb123182b3c79051dcd3"
    }
  ]
}
```
- **Response** (`200 OK`):
```json
{
  "status": "success",
  "accepted_count": 1,
  "last_synced_timestamp": 1750000000
}
```
- **Error Response** (`422 Unprocessable Entity` - Hash Validation Failure / Chain Break):
```json
{
  "detail": "Hash verification failed for record at timestamp 1750000000. Computed hash does not match provided hash."
}
```

---

### 2.3 Get Historical Readings
Fetches historical readings for a given device for dashboard visualization.

- **HTTP Method**: `GET`
- **Endpoint**: `/readings/{device_id}`
- **Query Parameters**:
  - `limit` (optional, default `100`, max `1000`): Number of recent records.
  - `start_time` (optional): Unix epoch start.
  - `end_time` (optional): Unix epoch end.
- **Response** (`200 OK`):
```json
{
  "device_id": "TS001",
  "total_records": 1,
  "readings": [
    {
      "device_id": "TS001",
      "timestamp": 1750000000,
      "gas_ppm": 42.381,
      "power_mW": 823.420,
      "plausibility": "NORMAL",
      "fingerprint_status": "SENSOR_OK",
      "previous_hash": "0000000000000000000000000000000000000000000000000000000000000000",
      "hash": "2e8e7995bb9576188874c028488bafb8a5973133134cbb123182b3c79051dcd3",
      "is_valid_hash": true,
      "is_valid_chain": true
    }
  ]
}
```

---

### 2.4 Verify Hash-Chain Integrity
Triggers full independent verification of the hash-chain for a device stored in PostgreSQL.

- **HTTP Method**: `GET`
- **Endpoint**: `/verify/{device_id}`
- **Response** (`200 OK`):
```json
{
  "device_id": "TS001",
  "chain_valid": true,
  "total_records_checked": 1250,
  "tampered_records": [],
  "chain_breaks": []
}
```
- **Response when Tampering Detected** (`200 OK`):
```json
{
  "device_id": "TS001",
  "chain_valid": false,
  "total_records_checked": 1250,
  "tampered_records": [
    {
      "timestamp": 1750000500,
      "expected_hash": "a591a6d40bf420404a011733cfb7b190d62c65bf0bcda32b57b277d9ad9f146e",
      "stored_hash": "bad1a6d40bf420404a011733cfb7b190d62c65bf0bcda32b57b277d9ad9f146e"
    }
  ],
  "chain_breaks": []
}
```

---

### 2.5 Get Active Security & Physical Alerts
Retrieves physical plausibility anomalies, sensor identity mismatches, or data tampering alerts.

- **HTTP Method**: `GET`
- **Endpoint**: `/alerts`
- **Query Parameters**:
  - `device_id` (optional): Filter by device.
- **Response** (`200 OK`):
```json
{
  "alerts": [
    {
      "alert_id": 1,
      "device_id": "TS001",
      "timestamp": 1750000200,
      "type": "PHYSICAL_PLAUSIBILITY_SUSPICIOUS",
      "severity": "HIGH",
      "message": "Gas spike detected (120 PPM) with no corresponding equipment power activity (0 mW)."
    },
    {
      "alert_id": 2,
      "device_id": "TS001",
      "timestamp": 1750000300,
      "type": "SENSOR_IDENTITY_MISMATCH",
      "severity": "CRITICAL",
      "message": "Warm-up curve similarity (0.62) fell below threshold (0.85). Possible unauthorized sensor replacement."
    }
  ]
}
```
