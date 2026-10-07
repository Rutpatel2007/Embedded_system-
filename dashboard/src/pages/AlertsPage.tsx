import { useState, useMemo } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api } from '../api/client';
import type { AlertItem } from '../api/types';
import { Card, CardHeader, CardContent } from '../components/ui/Card';
import { Badge } from '../components/ui/Badge';
import { Button, Skeleton } from '../components/ui/Primitives';
import { Table, TableHeader, TableBody, TableRow, TableHead, TableCell } from '../components/ui/Table';

export function AlertsPage() {
  const deviceId = 'TS001';
  const [selectedSeverity, setSelectedSeverity] = useState<string>('ALL');
  const [selectedType, setSelectedType] = useState<string>('ALL');
  const [activeAlert, setActiveAlert] = useState<AlertItem | null>(null);

  const {
    data,
    isLoading,
    isError,
    refetch,
    isFetching,
  } = useQuery({
    queryKey: ['alerts', deviceId],
    queryFn: () => api.getAlerts(deviceId),
    refetchInterval: 5000,
  });

  const rawAlerts = data?.alerts;
  const alerts = useMemo(() => rawAlerts ?? [], [rawAlerts]);

  const filteredAlerts = useMemo(() => {
    return alerts.filter((a) => {
      const matchSeverity =
        selectedSeverity === 'ALL' || a.severity === selectedSeverity;
      const matchType = selectedType === 'ALL' || a.type === selectedType;
      return matchSeverity && matchType;
    });
  }, [alerts, selectedSeverity, selectedType]);

  const uniqueTypes = useMemo(() => {
    const set = new Set<string>();
    alerts.forEach((a) => set.add(a.type));
    return Array.from(set);
  }, [alerts]);

  return (
    <div className="space-y-6">
      {/* Header & Controls */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 border-b border-hairline pb-4">
        <div>
          <h2 className="text-xl font-semibold tracking-tight text-ink font-mono-num">
            SYSTEM ALERTS & ANOMALIES
          </h2>
          <p className="text-xs text-muted mt-0.5">
            Physical plausibility violations, sensor replacement alarms, and cryptographic hash tamper detections.
          </p>
        </div>

        <Button
          size="sm"
          variant="outline"
          isLoading={isFetching}
          onClick={() => refetch()}
        >
          Refresh Alerts
        </Button>
      </div>

      {/* Filter Toolbar */}
      <div className="flex flex-wrap items-center gap-3 p-3 bg-surface border border-hairline text-xs font-mono-num">
        <div className="flex items-center gap-1.5">
          <span className="text-muted uppercase text-[11px] font-semibold">Severity:</span>
          <div className="flex border border-hairline bg-surface-elevated">
            {['ALL', 'CRITICAL', 'HIGH', 'MEDIUM', 'LOW'].map((sev) => (
              <button
                key={sev}
                onClick={() => setSelectedSeverity(sev)}
                className={`px-2 py-1 text-[11px] cursor-pointer ${
                  selectedSeverity === sev
                    ? 'bg-teal text-white font-semibold'
                    : 'text-muted hover:text-ink'
                }`}
              >
                {sev}
              </button>
            ))}
          </div>
        </div>

        <div className="flex items-center gap-1.5">
          <span className="text-muted uppercase text-[11px] font-semibold">Type:</span>
          <select
            value={selectedType}
            onChange={(e) => setSelectedType(e.target.value)}
            className="border border-hairline bg-surface-elevated px-2 py-1 text-[11px] text-ink focus:outline-none"
          >
            <option value="ALL">ALL TYPES</option>
            {uniqueTypes.map((t) => (
              <option key={t} value={t}>
                {t}
              </option>
            ))}
          </select>
        </div>

        <div className="ml-auto text-muted text-[11px]">
          Showing {filteredAlerts.length} of {alerts.length} events
        </div>
      </div>

      {/* Active Alert Details Drawer */}
      {activeAlert && (
        <Card className="border-brick/50 bg-brick-subtle/20">
          <CardHeader
            title={`ALERT #${activeAlert.alert_id} DETAILS`}
            description="Deep inspection of anomaly trigger."
            action={
              <Button size="sm" variant="outline" onClick={() => setActiveAlert(null)}>
                Dismiss
              </Button>
            }
          />
          <CardContent className="font-mono text-xs space-y-3">
            <div className="flex items-center gap-3">
              <Badge
                variant={activeAlert.severity === 'CRITICAL' ? 'critical' : 'warning'}
              >
                {activeAlert.severity}
              </Badge>
              <span className="font-semibold text-ink">{activeAlert.type}</span>
            </div>

            <div className="p-3 bg-surface-elevated border border-hairline text-ink leading-relaxed">
              {activeAlert.message}
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 gap-2 text-muted pt-2 border-t border-hairline">
              <div>Device ID: <span className="text-ink font-semibold">{activeAlert.device_id}</span></div>
              <div>Reported Epoch: <span className="text-ink font-semibold">{activeAlert.timestamp}</span></div>
              <div>ISO Timestamp: <span className="text-ink font-semibold">{new Date(activeAlert.timestamp * 1000).toISOString()}</span></div>
              <div>Authoritative Layer: <span className="text-ink font-semibold">Deterministic Firmware State</span></div>
            </div>
          </CardContent>
        </Card>
      )}

      {/* Filterable Alerts Table */}
      <Card>
        <CardHeader
          title="ALERT EVENT LOG"
          description="Click an alert row to review incident context."
        />
        <CardContent className="p-0">
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>ID</TableHead>
                <TableHead>SEVERITY</TableHead>
                <TableHead>TYPE</TableHead>
                <TableHead>TIMESTAMP</TableHead>
                <TableHead>DETAILS</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {isLoading ? (
                [...Array(4)].map((_, i) => (
                  <TableRow key={i}>
                    <TableCell colSpan={5}>
                      <Skeleton className="h-6 w-full" />
                    </TableCell>
                  </TableRow>
                ))
              ) : filteredAlerts.length > 0 ? (
                filteredAlerts.map((alert) => (
                  <TableRow
                    key={alert.alert_id}
                    onClick={() => setActiveAlert(alert)}
                    className="cursor-pointer"
                  >
                    <TableCell className="font-semibold text-ink">
                      #{alert.alert_id}
                    </TableCell>
                    <TableCell>
                      <Badge
                        variant={alert.severity === 'CRITICAL' ? 'critical' : 'warning'}
                        size="sm"
                      >
                        {alert.severity}
                      </Badge>
                    </TableCell>
                    <TableCell className="font-semibold text-ink text-[11px]">
                      {alert.type}
                    </TableCell>
                    <TableCell className="text-muted text-[11px]">
                      {new Date(alert.timestamp * 1000).toLocaleString([], {
                        month: 'short',
                        day: 'numeric',
                        hour: '2-digit',
                        minute: '2-digit',
                        second: '2-digit',
                      })}
                    </TableCell>
                    <TableCell className="text-muted truncate max-w-xs">
                      {alert.message}
                    </TableCell>
                  </TableRow>
                ))
              ) : (
                <TableRow>
                  <TableCell colSpan={5} className="text-center py-8 text-muted">
                    No active alerts matching criteria. System is operating normally.
                  </TableCell>
                </TableRow>
              )}
            </TableBody>
          </Table>
        </CardContent>
      </Card>

      {isError && (
        <div className="p-4 border border-brick bg-brick-subtle text-brick text-xs">
          Failed to load alerts from backend server.
        </div>
      )}
    </div>
  );
}
