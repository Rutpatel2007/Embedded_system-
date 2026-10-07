import { useState, useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api } from '../api/client';
import { Card, CardHeader, CardContent } from '../components/ui/Card';
import { Stat } from '../components/ui/Stat';
import { Badge } from '../components/ui/Badge';
import { Button, Skeleton } from '../components/ui/Primitives';

export function OverviewPage() {
  const deviceId = 'TS001';

  const {
    data: readingsData,
    isLoading: isReadingsLoading,
    isError: isReadingsError,
    refetch: refetchReadings,
  } = useQuery({
    queryKey: ['readings', deviceId],
    queryFn: () => api.getReadings(deviceId, { limit: 1 }),
    refetchInterval: 5000,
  });

  const {
    data: verifyData,
    isLoading: isVerifyLoading,
    refetch: refetchVerify,
  } = useQuery({
    queryKey: ['verify', deviceId],
    queryFn: () => api.verifyChain(deviceId),
  });

  const {
    data: alertsData,
    isLoading: isAlertsLoading,
  } = useQuery({
    queryKey: ['alerts', deviceId],
    queryFn: () => api.getAlerts(deviceId),
  });

  const latest = readingsData?.readings?.[0];
  const [nowSec, setNowSec] = useState<number>(() => Math.floor(Date.now() / 1000));

  useEffect(() => {
    const timer = setInterval(() => {
      setNowSec(Math.floor(Date.now() / 1000));
    }, 5000);
    return () => clearInterval(timer);
  }, []);

  const lastSyncAge = latest ? Math.max(0, nowSec - latest.timestamp) : null;

  return (
    <div className="space-y-6">
      {/* Editorial thesis header */}
      <div className="border-b border-hairline pb-4">
        <h2 className="text-xl font-semibold tracking-tight text-ink font-mono-num">
          SYSTEM OVERVIEW
        </h2>
        <p className="text-sm text-muted mt-1 max-w-3xl leading-relaxed">
          TrueSense binds uncalibrated environmental proxy telemetry to real-time industrial electrical power states,
          verifying physical plausibility and sensor silicon continuity before committing records into a tamper-evident SHA-256 hash log.
        </p>
      </div>

      {/* KPI Stats Grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        <Stat
          label="Active Device Node"
          value={deviceId}
          subtext="Factory Exhaust Node 1"
          statusBadge={<Badge variant="verified" size="sm">ENROLLED</Badge>}
        />

        <Stat
          label="Chain Integrity"
          value={
            isVerifyLoading ? (
              <Skeleton className="h-7 w-24" />
            ) : verifyData?.chain_valid ? (
              'VERIFIED'
            ) : verifyData?.chain_valid === false ? (
              'COMPROMISED'
            ) : (
              'UNKNOWN'
            )
          }
          subtext={
            verifyData ? (
              `${verifyData.total_records_checked} blocks evaluated`
            ) : (
              'Awaiting backend check'
            )
          }
          statusBadge={
            verifyData?.chain_valid ? (
              <Badge variant="verified" size="sm">0 BREAKS</Badge>
            ) : verifyData ? (
              <Badge variant="critical" size="sm">{verifyData.chain_breaks + verifyData.tampered_records} FAULTS</Badge>
            ) : undefined
          }
        />

        <Stat
          label="Ingest Freshness"
          value={
            latest ? (
              `${latest.timestamp}`
            ) : isReadingsLoading ? (
              <Skeleton className="h-7 w-28" />
            ) : (
              'NO DATA'
            )
          }
          unit={latest ? 'epoch' : ''}
          subtext={
            lastSyncAge !== null
              ? `Reported timestamp: ${new Date(latest!.timestamp * 1000).toLocaleTimeString()}`
              : 'Waiting for node synchronization'
          }
          statusBadge={
            latest ? (
              <Badge variant="verified" size="sm">SYNCED</Badge>
            ) : (
              <Badge variant="warning" size="sm">OFFLINE</Badge>
            )
          }
        />

        <Stat
          label="Security & Anomaly Alerts"
          value={
            isAlertsLoading ? (
              <Skeleton className="h-7 w-12" />
            ) : (
              alertsData?.alerts.length ?? 0
            )
          }
          subtext="Physical & cryptographic flags"
          statusBadge={
            alertsData && alertsData.alerts.length > 0 ? (
              <Badge variant="warning" size="sm">{alertsData.alerts.length} ACTIVE</Badge>
            ) : (
              <Badge variant="verified" size="sm">CLEAN</Badge>
            )
          }
        />
      </div>

      {/* The Three-Layer Trust Chain Architecture Diagram */}
      <Card>
        <CardHeader
          title="THREE-LAYER TRUST CHAIN"
          description="Every incoming record must satisfy physical plausibility and sensor continuity before cryptographic commitment."
        />
        <CardContent>
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4 relative">
            {/* Layer 1: Physical Plausibility */}
            <div className="border border-hairline p-4 bg-surface flex flex-col justify-between">
              <div>
                <div className="flex items-center justify-between gap-2 mb-2">
                  <span className="text-[11px] font-mono text-muted uppercase font-semibold">
                    Layer 01
                  </span>
                  {latest?.plausibility === 'NORMAL' && (
                    <Badge variant="verified" size="sm">PASS: NORMAL</Badge>
                  )}
                  {latest?.plausibility === 'PLAUSIBLE' && (
                    <Badge variant="verified" size="sm">PASS: PLAUSIBLE</Badge>
                  )}
                  {latest?.plausibility === 'SUSPICIOUS' && (
                    <Badge variant="critical" size="sm">SUSPICIOUS</Badge>
                  )}
                  {!latest && <Badge variant="neutral" size="sm">PENDING</Badge>}
                </div>
                <h4 className="text-sm font-semibold text-ink">Physical Plausibility Engine</h4>
                <p className="text-xs text-muted mt-1 leading-relaxed">
                  Cross-modal correlation between MQ-135 gas signal voltage and INA219 trailing equipment power draw (90s window).
                </p>
              </div>

              <div className="mt-4 pt-3 border-t border-hairline/60 text-xs font-mono-num space-y-1">
                <div className="flex justify-between text-muted">
                  <span>Gas Signal (V):</span>
                  <span className="text-ink font-semibold">
                    {latest ? `${latest.gas_ppm.toFixed(3)} V` : '—'}
                  </span>
                </div>
                <div className="flex justify-between text-muted">
                  <span>Equipment Draw:</span>
                  <span className="text-ink font-semibold">
                    {latest ? `${latest.power_mW.toFixed(1)} mW` : '—'}
                  </span>
                </div>
              </div>
            </div>

            {/* Layer 2: Sensor Identity */}
            <div className="border border-hairline p-4 bg-surface flex flex-col justify-between">
              <div>
                <div className="flex items-center justify-between gap-2 mb-2">
                  <span className="text-[11px] font-mono text-muted uppercase font-semibold">
                    Layer 02
                  </span>
                  {latest?.fingerprint_status === 'SENSOR_OK' && (
                    <Badge variant="verified" size="sm">SENSOR_OK</Badge>
                  )}
                  {latest?.fingerprint_status === 'SENSOR_IDENTITY_MISMATCH' && (
                    <Badge variant="critical" size="sm">MISMATCH</Badge>
                  )}
                  {!latest && <Badge variant="neutral" size="sm">PENDING</Badge>}
                </div>
                <h4 className="text-sm font-semibold text-ink">Sensor Identity Fingerprint</h4>
                <p className="text-xs text-muted mt-1 leading-relaxed">
                  Statistical hardware profile matching (thermal warm-up curve cosine similarity &gt; 0.85 in NVS) against enrolled sensor.
                </p>
              </div>

              <div className="mt-4 pt-3 border-t border-hairline/60 text-xs font-mono-num space-y-1">
                <div className="flex justify-between text-muted">
                  <span>Cosine Similarity:</span>
                  <span className="text-ink font-semibold">
                    {latest?.fingerprint_status === 'SENSOR_OK' ? '≥ 0.85 (Enrolled)' : '< 0.85 (Anomalous)'}
                  </span>
                </div>
                <div className="flex justify-between text-muted">
                  <span>NVS Profile:</span>
                  <span className="text-ink font-semibold">20-Point Curve</span>
                </div>
              </div>
            </div>

            {/* Layer 3: Tamper-Evident Hash Log */}
            <div className="border border-hairline p-4 bg-surface flex flex-col justify-between">
              <div>
                <div className="flex items-center justify-between gap-2 mb-2">
                  <span className="text-[11px] font-mono text-muted uppercase font-semibold">
                    Layer 03
                  </span>
                  {verifyData?.chain_valid ? (
                    <Badge variant="verified" size="sm">CHAIN INTACT</Badge>
                  ) : verifyData?.chain_valid === false ? (
                    <Badge variant="critical" size="sm">CHAIN BROKEN</Badge>
                  ) : (
                    <Badge variant="neutral" size="sm">PENDING</Badge>
                  )}
                </div>
                <h4 className="text-sm font-semibold text-ink">Tamper-Evident SHA-256 Log</h4>
                <p className="text-xs text-muted mt-1 leading-relaxed">
                  Cryptographic forward chain linking each reading block to preceding hash digest H(n-1) on microSD and backend.
                </p>
              </div>

              <div className="mt-4 pt-3 border-t border-hairline/60 text-xs font-mono-num space-y-1">
                <div className="flex justify-between text-muted">
                  <span>Genesis State:</span>
                  <span className="text-ink font-semibold">0x00...00 (64 zeros)</span>
                </div>
                <div className="flex justify-between text-muted">
                  <span>Hash Algorithm:</span>
                  <span className="text-ink font-semibold">SHA-256 (Canonical)</span>
                </div>
              </div>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Latest Synced Block & Error State */}
      {isReadingsError && (
        <div className="border border-brick/40 bg-brick-subtle p-4 text-xs text-brick flex items-center justify-between">
          <div>
            <strong>Backend Communication Failure:</strong> Unable to retrieve telemetry from /api/v1/readings/{deviceId}. Ensure the FastAPI server is running on port 8000.
          </div>
          <Button size="sm" variant="outline" onClick={() => { refetchReadings(); refetchVerify(); }}>
            Retry Ingestion
          </Button>
        </div>
      )}

      {latest && (
        <Card>
          <CardHeader
            title="LATEST CANONICAL BLOCK COMMIT"
            description="Exact raw record committed to the ledger during the most recent node sync."
          />
          <CardContent>
            <div className="bg-surface border border-hairline p-4 font-mono text-xs space-y-2 overflow-x-auto font-mono-num">
              <div className="grid grid-cols-1 md:grid-cols-2 gap-2 text-muted">
                <div>
                  <span className="text-ink font-semibold">Device ID:</span> {latest.device_id}
                </div>
                <div>
                  <span className="text-ink font-semibold">Timestamp:</span> {latest.timestamp} ({new Date(latest.timestamp * 1000).toISOString()})
                </div>
                <div>
                  <span className="text-ink font-semibold">Gas Signal (Proxy V):</span> {latest.gas_ppm.toFixed(3)}
                </div>
                <div>
                  <span className="text-ink font-semibold">Equipment Power:</span> {latest.power_mW.toFixed(3)} mW
                </div>
              </div>
              <div className="pt-2 border-t border-hairline">
                <div className="text-muted truncate">
                  <span className="text-ink font-semibold">Previous Hash:</span> {latest.previous_hash}
                </div>
                <div className="text-muted truncate">
                  <span className="text-ink font-semibold">Record Hash:</span> {latest.hash}
                </div>
              </div>
            </div>
          </CardContent>
        </Card>
      )}
    </div>
  );
}
