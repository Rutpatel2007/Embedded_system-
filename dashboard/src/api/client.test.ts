import { describe, it, expect, vi, beforeEach } from 'vitest';
import { api, ApiClientError } from './client';

describe('api client', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('fetches health status successfully', async () => {
    const mockHealth = { status: 'healthy' };
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => mockHealth,
    } as unknown as Response);

    const result = await api.getHealth();
    expect(result).toEqual(mockHealth);
    expect(globalThis.fetch).toHaveBeenCalledWith(
      expect.stringContaining('/health'),
      expect.objectContaining({
        headers: { 'Content-Type': 'application/json' },
      })
    );
  });

  it('handles API error responses cleanly', async () => {
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: false,
      status: 404,
      statusText: 'Not Found',
      json: async () => ({ detail: 'Device TS001 is not registered' }),
    } as unknown as Response);

    await expect(api.verifyChain('TS001')).rejects.toThrow(ApiClientError);
    await expect(api.verifyChain('TS001')).rejects.toMatchObject({
      status: 404,
      detail: 'Device TS001 is not registered',
    });
  });

  it('requests historical readings with query parameters', async () => {
    const mockResponse = {
      device_id: 'TS001',
      total_records: 2,
      readings: [],
    };

    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => mockResponse,
    } as unknown as Response);

    const result = await api.getReadings('TS001', { limit: 50, start_time: 1750000000 });
    expect(result.device_id).toBe('TS001');
    expect(globalThis.fetch).toHaveBeenCalledWith(
      expect.stringContaining('/readings/TS001?limit=50&start_time=1750000000'),
      expect.anything()
    );
  });
});
