export type Plausibility = 'NORMAL' | 'PLAUSIBLE' | 'SUSPICIOUS';
export type FingerprintStatus = 'SENSOR_OK' | 'SENSOR_IDENTITY_MISMATCH';
export type AlertSeverity = 'LOW' | 'MEDIUM' | 'HIGH' | 'CRITICAL';
export type AlertType = 'PHYSICAL_PLAUSIBILITY_SUSPICIOUS' | 'SENSOR_IDENTITY_MISMATCH' | 'DATA_TAMPERING' | string;

export interface HealthResponse {
  status: string;
}

export interface DeviceRegisterRequest {
  device_id: string;
  name: string;
  enrolled_fingerprint: number[]; // exactly 20 numbers
}

export interface DeviceRegisterResponse {
  status: string;
  device_id: string;
  message: string;
}

export interface ReadingIngestItem {
  device_id: string;
  timestamp: number;
  gas_ppm: number;
  power_mW: number;
  plausibility: Plausibility;
  fingerprint_status: FingerprintStatus;
  previous_hash: string;
  hash: string;
}

export interface ReadingIngestRequest {
  device_id: string;
  readings: ReadingIngestItem[];
}

export interface ReadingIngestResponse {
  status: string;
  accepted_count: number;
  last_synced_timestamp: number;
}

export interface HistoricalReadingItem {
  device_id: string;
  timestamp: number;
  gas_ppm: number; // Gas signal proxy in Volts
  power_mW: number;
  plausibility: Plausibility;
  fingerprint_status: FingerprintStatus;
  previous_hash: string;
  hash: string;
  is_valid_hash: boolean;
  is_valid_chain: boolean;
}

export interface HistoricalReadingsResponse {
  device_id: string;
  total_records: number;
  readings: HistoricalReadingItem[];
}

export interface ChainVerificationResponse {
  device_id: string;
  chain_valid: boolean;
  total_records_checked: number;
  tampered_records: number;
  chain_breaks: number;
}

export interface AlertItem {
  alert_id: number;
  device_id: string;
  timestamp: number;
  type: AlertType;
  severity: AlertSeverity;
  message: string;
}

export interface AlertsResponse {
  alerts: AlertItem[];
}

export interface ApiError {
  status: number;
  detail: string;
}
