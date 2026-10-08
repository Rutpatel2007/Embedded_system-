import type {
  HealthResponse,
  DeviceRegisterRequest,
  DeviceRegisterResponse,
  ReadingIngestRequest,
  ReadingIngestResponse,
  HistoricalReadingsResponse,
  ChainVerificationResponse,
  AlertsResponse,
} from './types';

// In dev with Vite proxy, use relative path '/api/v1' by default
// In production or custom setup, use VITE_API_BASE_URL + '/api/v1'
const BASE_URL = import.meta.env.VITE_API_BASE_URL 
  ? `${import.meta.env.VITE_API_BASE_URL.replace(/\/+$/, '')}/api/v1` 
  : '/api/v1';

export class ApiClientError extends Error {
  status: number;
  detail: string;

  constructor(status: number, detail: string) {
    super(`API Error ${status}: ${detail}`);
    this.name = 'ApiClientError';
    this.status = status;
    this.detail = detail;
  }
}

async function request<T>(endpoint: string, options?: RequestInit): Promise<T> {
  const url = `${BASE_URL}${endpoint}`;
  try {
    const response = await fetch(url, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        ...options?.headers,
      },
    });

    if (!response.ok) {
      let detail = `HTTP ${response.status} ${response.statusText}`;
      try {
        const errorJson = await response.json();
        if (errorJson && typeof errorJson.detail === 'string') {
          detail = errorJson.detail;
        } else if (errorJson && typeof errorJson.message === 'string') {
          detail = errorJson.message;
        }
      } catch {
        // Response was not JSON
      }
      throw new ApiClientError(response.status, detail);
    }

    return (await response.json()) as T;
  } catch (err: unknown) {
    if (err instanceof ApiClientError) {
      throw err;
    }
    const message = err instanceof Error ? err.message : 'Network error or backend unreachable';
    throw new ApiClientError(0, message);
  }
}

export const api = {
  getHealth(): Promise<HealthResponse> {
    return request<HealthResponse>('/health');
  },

  registerDevice(payload: DeviceRegisterRequest): Promise<DeviceRegisterResponse> {
    return request<DeviceRegisterResponse>('/devices/register', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  ingestReadings(payload: ReadingIngestRequest): Promise<ReadingIngestResponse> {
    return request<ReadingIngestResponse>('/readings/ingest', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  getReadings(
    deviceId: string,
    params?: { limit?: number; start_time?: number; end_time?: number }
  ): Promise<HistoricalReadingsResponse> {
    const query = new URLSearchParams();
    if (params?.limit !== undefined) query.set('limit', String(params.limit));
    if (params?.start_time !== undefined) query.set('start_time', String(params.start_time));
    if (params?.end_time !== undefined) query.set('end_time', String(params.end_time));
    const qs = query.toString();
    return request<HistoricalReadingsResponse>(`/readings/${deviceId}${qs ? `?${qs}` : ''}`);
  },

  verifyChain(deviceId: string): Promise<ChainVerificationResponse> {
    return request<ChainVerificationResponse>(`/verify/${deviceId}`);
  },

  getAlerts(deviceId?: string): Promise<AlertsResponse> {
    const query = deviceId ? `?device_id=${encodeURIComponent(deviceId)}` : '';
    return request<AlertsResponse>(`/alerts${query}`);
  },

  getMLStatus(): Promise<unknown> {
    return request<unknown>('/ml/status');
  },
};
