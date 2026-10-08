import { useState, useMemo } from 'react';
import { useQuery } from '@tanstack/react-query';
import {
  ResponsiveContainer,
  LineChart,
  Line,
  XAxis,
  YAxis,
  Tooltip,
  CartesianGrid,
  ReferenceDot,
} from 'recharts';
import { api } from '../api/client';
import { Card, CardHeader, CardContent } from '../components/ui/Card';
import { Badge } from '../components/ui/Badge';
import { Button, Skeleton } from '../components/ui/Primitives';

export function LiveMonitorPage() {
  const deviceId = 'TS001';
  const [limit, setLimit] = useState<number>(100);
  const [isPollingPaused, setIsPollingPaused] = useState<boolean>(false);

  const {
    data,
    isLoading,
    isError,
    refetch,
    isFetching,
  } = useQuery({
    queryKey: ['liveReadings', deviceId, limit],
    queryFn: () => api.getReadings(deviceId, { limit }),
    refetchInterval: isPollingPaused ? false : 3000,
  });

  // Reorder ascending chronologically for chart display
  const chartData = useMemo(() => {
    if (!data?.readings) return [];
    return [...data.readings].reverse().map((r) => ({
      ...r,
      timeLabel: new Date(r.timestamp * 1000).toLocaleTimeString([], {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
      }),
      isSuspicious: r.plausibility === 'SUSPICIOUS',
      isMismatch: r.fingerprint_status === 'SENSOR_IDENTITY_MISMATCH',
    }));
  }, [data]);

  const latestReading = data?.readings?.[0];

  return (
    <div className="space-y-6">
      {/* Page Header and Controls */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 border-b border-hairline pb-4">
        <div>
          <h2 className="text-xl font-semibold tracking-tight text-ink font-mono-num">
            LIVE TELEMETRY MONITOR
          </h2>
          <p className="text-xs text-muted mt-0.5">
            Real-time cross-modal stream: Gas Sensor Voltage (proxy) aligned with Equipment Power draw.
          </p>
        </div>

        <div className="flex items-center gap-2">
          {/* Range Selector */}
          <div className="flex items-center border border-hairline bg-surface p-0.5 text-xs font-mono-num">
            {[50, 100, 200, 500].map((count) => (
              <button
                key={count}
                onClick={() => setLimit(count)}
                className={`px-2.5 py-1 text-xs cursor-pointer ${
                  limit === count
                    ? 'bg-surface-elevated text-ink font-semibold shadow-2xs'
                    : 'text-muted hover:text-ink'
                }`}
              >
                {count} pts
              </button>
            ))}
          </div>

          {/* Polling Pause Toggle */}
          <Button
            size="sm"
            variant={isPollingPaused ? 'primary' : 'outline'}
            onClick={() => setIsPollingPaused(!isPollingPaused)}
          >
            {isPollingPaused ? 'Resume Polling' : 'Pause Polling'}
          </Button>

          <Button
            size="sm"
            variant="outline"
            isLoading={isFetching}
            onClick={() => refetch()}
            title="Manual refresh"
          >
            Refresh
          </Button>
        </div>
      </div>

      {/* Latest Sample Status Panel */}
      <Card>
        <CardHeader
          title="LATEST ACQUISITION TELEMETRY"
          description="Immediate verification attributes from the most recently ingested packet."
          action={
            <div className="flex items-center gap-2 font-mono-num text-xs">
              <span className="text-muted">STATUS:</span>
              {latestReading?.plausibility === 'NORMAL' && (
                <Badge variant="verified">NORMAL</Badge>
              )}
              {latestReading?.plausibility === 'PLAUSIBLE' && (
                <Badge variant="verified">PLAUSIBLE</Badge>
              )}
              {latestReading?.plausibility === 'SUSPICIOUS' && (
                <Badge variant="critical">SUSPICIOUS SPIKE</Badge>
              )}
              {!latestReading && <Badge variant="neutral">NO SIGNAL</Badge>}
            </div>
          }
        />
        <CardContent>
          {isLoading ? (
            <div className="grid grid-cols-2 sm:grid-cols-4 gap-4">
              <Skeleton className="h-16 w-full" />
              <Skeleton className="h-16 w-full" />
              <Skeleton className="h-16 w-full" />
              <Skeleton className="h-16 w-full" />
            </div>
          ) : latestReading ? (
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4 font-mono-num">
              <div className="p-3 border border-hairline bg-surface">
                <span className="text-[11px] text-muted block uppercase">Gas Signal (Proxy)</span>
                <span className="text-xl font-semibold text-ink">
                  {latestReading.gas_ppm.toFixed(3)}
                </span>
                <span className="text-xs text-muted ml-1">V (uncalibrated)</span>
              </div>

              <div className="p-3 border border-hairline bg-surface">
                <span className="text-[11px] text-muted block uppercase">Equipment Draw</span>
                <span className="text-xl font-semibold text-ink">
                  {latestReading.power_mW.toFixed(1)}
                </span>
                <span className="text-xs text-muted ml-1">mW</span>
              </div>

              <div className="p-3 border border-hairline bg-surface">
                <span className="text-[11px] text-muted block uppercase">Fingerprint Identity</span>
                <div className="mt-1">
                  {latestReading.fingerprint_status === 'SENSOR_OK' ? (
                    <Badge variant="verified" size="sm">SENSOR_OK</Badge>
                  ) : (
                    <Badge variant="critical" size="sm">MISMATCH</Badge>
                  )}
                </div>
              </div>

              <div className="p-3 border border-hairline bg-surface">
                <span className="text-[11px] text-muted block uppercase">Block Hash Verification</span>
                <div className="mt-1 flex items-center gap-1.5">
                  {latestReading.is_valid_hash ? (
                    <Badge variant="verified" size="sm">HASH VALID</Badge>
                  ) : (
                    <Badge variant="critical" size="sm">TAMPERED</Badge>
                  )}
                  {latestReading.is_valid_chain ? (
                    <Badge variant="verified" size="sm">CHAIN LINKED</Badge>
                  ) : (
                    <Badge variant="warning" size="sm">CHAIN BREAK</Badge>
                  )}
                </div>
              </div>
            </div>
          ) : (
            <div className="text-xs text-muted font-mono p-4 text-center border border-dashed border-hairline">
              No historical data returned for device {deviceId}. Run the ingest script to load simulation data.
            </div>
          )}
        </CardContent>
      </Card>

      {/* Time-Aligned Dual Telemetry Charts */}
      <div className="space-y-6">
        {/* Gas Signal Chart */}
        <Card>
          <CardHeader
            title="GAS SIGNAL (V, UNCALIBRATED PROXY)"
            description="Gas signal (V, uncalibrated proxy) from MQ-135 load resistor."
          />
          <CardContent>
            <div className="h-64 w-full">
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={chartData} margin={{ top: 10, right: 10, left: 0, bottom: 0 }}>
                  <CartesianGrid strokeDasharray="3 3" stroke="var(--border-hairline)" opacity={0.6} />
                  <XAxis
                    dataKey="timeLabel"
                    stroke="var(--text-muted)"
                    fontSize={11}
                    tickLine={false}
                    interval="preserveStartEnd"
                  />
                  <YAxis
                    stroke="var(--text-muted)"
                    fontSize={11}
                    tickLine={false}
                    domain={['auto', 'auto']}
                    unit="V"
                  />
                  <Tooltip
                    contentStyle={{
                      backgroundColor: 'var(--bg-surface-elevated)',
                      borderColor: 'var(--border-hairline)',
                      borderRadius: 0,
                      fontSize: '12px',
                      fontFamily: 'var(--font-mono)',
                    }}
                    formatter={(val: unknown) => [
                      `${Number(val).toFixed(3)} V (proxy)`,
                      'Gas Signal',
                    ]}
                  />
                  <Line
                    type="monotone"
                    dataKey="gas_ppm"
                    stroke="#0F5C5A"
                    strokeWidth={1.5}
                    dot={false}
                    isAnimationActive={true}
                  />
                  {/* Mark suspicious points */}
                  {chartData.map((entry, index) =>
                    entry.isSuspicious ? (
                      <ReferenceDot
                        key={`gas-suspicious-${index}`}
                        x={entry.timeLabel}
                        y={entry.gas_ppm}
                        r={4}
                        fill="#A63A2B"
                        stroke="#FAF2E6"
                        strokeWidth={1}
                      />
                    ) : null
                  )}
                </LineChart>
              </ResponsiveContainer>
            </div>
          </CardContent>
        </Card>

        {/* Equipment Power Draw Chart */}
        <Card>
          <CardHeader
            title="EQUIPMENT POWER DRAW (mW)"
            description="INA219 bidirectional high-side current & bus voltage monitor. Verified against physical plausibility threshold (50 mW)."
          />
          <CardContent>
            <div className="h-64 w-full">
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={chartData} margin={{ top: 10, right: 10, left: 0, bottom: 0 }}>
                  <CartesianGrid strokeDasharray="3 3" stroke="var(--border-hairline)" opacity={0.6} />
                  <XAxis
                    dataKey="timeLabel"
                    stroke="var(--text-muted)"
                    fontSize={11}
                    tickLine={false}
                    interval="preserveStartEnd"
                  />
                  <YAxis
                    stroke="var(--text-muted)"
                    fontSize={11}
                    tickLine={false}
                    domain={[0, 'auto']}
                    unit="mW"
                  />
                  <Tooltip
                    contentStyle={{
                      backgroundColor: 'var(--bg-surface-elevated)',
                      borderColor: 'var(--border-hairline)',
                      borderRadius: 0,
                      fontSize: '12px',
                      fontFamily: 'var(--font-mono)',
                    }}
                    formatter={(val: unknown) => [
                      `${Number(val).toFixed(1)} mW`,
                      'Power Draw',
                    ]}
                  />
                  <Line
                    type="monotone"
                    dataKey="power_mW"
                    stroke="#5A5E63"
                    strokeWidth={1.5}
                    dot={false}
                    isAnimationActive={true}
                  />
                </LineChart>
              </ResponsiveContainer>
            </div>
          </CardContent>
        </Card>
      </div>

      {isError && (
        <div className="p-4 border border-brick bg-brick-subtle text-brick text-xs">
          Error retrieving telemetry from server. Check that backend is active at http://localhost:8000.
        </div>
      )}
    </div>
  );
}
