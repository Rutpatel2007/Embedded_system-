import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api } from '../api/client';
import { Card, CardHeader, CardContent } from '../components/ui/Card';
import { Badge } from '../components/ui/Badge';
import { Button, Skeleton } from '../components/ui/Primitives';
import { Table, TableHeader, TableBody, TableRow, TableHead, TableCell } from '../components/ui/Table';

export function IntegrityPage() {
  const deviceId = 'TS001';
  const queryClient = useQueryClient();
  const [selectedHash, setSelectedHash] = useState<string | null>(null);

  const {
    data: verifyData,
    isLoading: isVerifyLoading,
    isFetching: isVerifyFetching,
  } = useQuery({
    queryKey: ['verify', deviceId],
    queryFn: () => api.verifyChain(deviceId),
  });

  const {
    data: readingsData,
    isLoading: isReadingsLoading,
  } = useQuery({
    queryKey: ['readings', deviceId, 100],
    queryFn: () => api.getReadings(deviceId, { limit: 100 }),
  });

  const verifyMutation = useMutation({
    mutationFn: () => api.verifyChain(deviceId),
    onSuccess: (data) => {
      queryClient.setQueryData(['verify', deviceId], data);
      queryClient.invalidateQueries({ queryKey: ['readings', deviceId] });
      queryClient.invalidateQueries({ queryKey: ['alerts', deviceId] });
    },
  });

  const readings = readingsData?.readings ?? [];

  return (
    <div className="space-y-6">
      {/* Header with trigger button */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 border-b border-hairline pb-4">
        <div>
          <h2 className="text-xl font-semibold tracking-tight text-ink font-mono-num">
            HASH-CHAIN INTEGRITY VERIFIER
          </h2>
          <p className="text-xs text-muted mt-0.5">
            Full cryptographic walk over all stored blocks for {deviceId}: checks SHA-256 canonical digests and sequential continuity.
          </p>
        </div>

        <Button
          variant="primary"
          isLoading={isVerifyLoading || isVerifyFetching || verifyMutation.isPending}
          onClick={() => verifyMutation.mutate()}
        >
          Verify Entire Chain
        </Button>
      </div>

      {/* Verification Verdict Banner */}
      <div
        className={`p-4 border font-mono-num flex flex-col sm:flex-row sm:items-center justify-between gap-4 ${
          verifyData?.chain_valid
            ? 'bg-teal-subtle border-teal text-teal'
            : verifyData && !verifyData.chain_valid
            ? 'bg-brick-subtle border-brick text-brick'
            : 'bg-surface border-hairline text-muted'
        }`}
      >
        <div className="flex items-start gap-3">
          <div className="mt-0.5">
            {verifyData?.chain_valid ? (
              <span className="inline-block w-3 h-3 bg-teal rounded-full" />
            ) : verifyData ? (
              <span className="inline-block w-3 h-3 bg-brick rounded-full" />
            ) : (
              <span className="inline-block w-3 h-3 bg-muted rounded-full" />
            )}
          </div>
          <div>
            <div className="text-sm font-bold tracking-tight">
              {verifyData?.chain_valid
                ? 'CRYPTOGRAPHIC CHAIN VERIFIED — INTACT'
                : verifyData && !verifyData.chain_valid
                ? 'INTEGRITY BREACH DETECTED — CHAIN COMPROMISED'
                : 'STATUS UNKNOWN — CLICK VERIFY TO AUDIT'}
            </div>
            <div className="text-xs mt-0.5 opacity-90">
              {verifyData
                ? `Audited ${verifyData.total_records_checked} sequential records. Tampered records: ${verifyData.tampered_records}. Broken link steps: ${verifyData.chain_breaks}.`
                : 'Verification has not run for this session.'}
            </div>
          </div>
        </div>

        <div className="text-xs sm:text-right shrink-0">
          <div>DEVICE ID: {deviceId}</div>
          <div className="opacity-75">CANONICAL: SHA-256 (Pipe Delimited)</div>
        </div>
      </div>

      {/* Chain Visualizer Block Linkage */}
      <Card>
        <CardHeader
          title="SEQUENTIAL BLOCK LINKAGE VISUALIZER"
          description="Interactive chain view. Each block links its payload to the previous block's digest H(n-1). Click a block to inspect."
        />
        <CardContent>
          {isReadingsLoading ? (
            <div className="flex gap-2 overflow-x-auto py-2">
              {[...Array(6)].map((_, i) => (
                <Skeleton key={i} className="h-20 w-44 shrink-0" />
              ))}
            </div>
          ) : readings.length > 0 ? (
            <div className="overflow-x-auto pb-3 pt-1">
              <div className="flex items-center gap-2 min-w-max">
                {readings.slice(0, 15).reverse().map((rec, idx) => {
                  const isTampered = !rec.is_valid_hash || !rec.is_valid_chain;
                  const isSelected = selectedHash === rec.hash;

                  return (
                    <div key={rec.hash} className="flex items-center">
                      <div
                        onClick={() => setSelectedHash(rec.hash)}
                        className={`p-3 border text-xs font-mono w-48 shrink-0 cursor-pointer transition-colors duration-100 ${
                          isSelected
                            ? 'ring-2 ring-teal border-teal'
                            : isTampered
                            ? 'border-brick bg-brick-subtle text-brick'
                            : 'border-hairline bg-surface hover:bg-surface-elevated'
                        }`}
                      >
                        <div className="flex items-center justify-between text-[11px] mb-1 font-semibold">
                          <span>BLOCK #{readings.length - idx}</span>
                          {isTampered ? (
                            <span className="text-brick text-[10px]">CORRUPT</span>
                          ) : (
                            <span className="text-teal text-[10px]">VALID</span>
                          )}
                        </div>
                        <div className="text-[10px] text-muted truncate">
                          T: {rec.timestamp}
                        </div>
                        <div className="text-[10px] text-muted truncate">
                          Gas: {rec.gas_ppm.toFixed(3)}V | P: {rec.power_mW.toFixed(0)}mW
                        </div>
                        <div className="text-[9px] text-muted/80 truncate mt-1 pt-1 border-t border-hairline/60">
                          H: {rec.hash.substring(0, 10)}...
                        </div>
                      </div>

                      {/* Connection arrow */}
                      {idx < Math.min(readings.length, 15) - 1 && (
                        <div className="px-1 text-muted text-xs select-none">
                          {isTampered ? (
                            <span className="text-brick font-bold">⤬</span>
                          ) : (
                            <span className="text-teal">→</span>
                          )}
                        </div>
                      )}
                    </div>
                  );
                })}
              </div>
            </div>
          ) : (
            <div className="text-xs text-muted text-center py-6 font-mono">
              No readings in database to display in visualizer.
            </div>
          )}
        </CardContent>
      </Card>

      {/* Selected Block Inspection Detail Drawer */}
      {selectedHash && (() => {
        const item = readings.find((r) => r.hash === selectedHash);
        if (!item) return null;
        return (
          <Card className="border-teal/40 bg-teal-subtle/10">
            <CardHeader
              title={`INSPECTING BLOCK: ${item.hash.substring(0, 16)}...`}
              description="Detailed internal values and serialization verification."
              action={
                <Button size="sm" variant="outline" onClick={() => setSelectedHash(null)}>
                  Close
                </Button>
              }
            />
            <CardContent className="font-mono text-xs space-y-2">
              <div className="grid grid-cols-1 md:grid-cols-2 gap-2 text-muted">
                <div>Device ID: <span className="text-ink font-semibold">{item.device_id}</span></div>
                <div>Epoch Timestamp: <span className="text-ink font-semibold">{item.timestamp}</span></div>
                <div>Gas signal (V, uncalibrated proxy): <span className="text-ink font-semibold">{item.gas_ppm.toFixed(3)}</span></div>
                <div>Power Draw: <span className="text-ink font-semibold">{item.power_mW.toFixed(3)} mW</span></div>
                <div>Plausibility Status: <span className="text-ink font-semibold">{item.plausibility}</span></div>
                <div>Fingerprint Match: <span className="text-ink font-semibold">{item.fingerprint_status}</span></div>
              </div>
              <div className="pt-2 border-t border-hairline space-y-1">
                <div className="text-muted break-all">
                  Previous Hash H(n-1): <span className="text-ink font-semibold">{item.previous_hash}</span>
                </div>
                <div className="text-muted break-all">
                  Committed Hash H(n): <span className="text-ink font-semibold">{item.hash}</span>
                </div>
                <div className="pt-1 flex gap-2">
                  <Badge variant={item.is_valid_hash ? 'verified' : 'critical'}>
                    {item.is_valid_hash ? 'Recomputed Hash Matches' : 'Computed Hash Mismatch (Tampered)'}
                  </Badge>
                  <Badge variant={item.is_valid_chain ? 'verified' : 'critical'}>
                    {item.is_valid_chain ? 'Sequential Chain Intact' : 'Previous Hash Mismatch (Broken)'}
                  </Badge>
                </div>
              </div>
            </CardContent>
          </Card>
        );
      })()}

      {/* Per-record Verification Audit Table */}
      <Card>
        <CardHeader
          title="LEDGER AUDIT LOG"
          description="Detailed table of latest historical records with per-record hash and continuity verification flags."
        />
        <CardContent className="p-0">
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>TIMESTAMP</TableHead>
                <TableHead>GAS SIGNAL (V)</TableHead>
                <TableHead>POWER (mW)</TableHead>
                <TableHead>PLAUSIBILITY</TableHead>
                <TableHead>FINGERPRINT</TableHead>
                <TableHead>HASH DIGEST</TableHead>
                <TableHead>HASH VALIDITY</TableHead>
                <TableHead>CHAIN LINK</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {isReadingsLoading ? (
                [...Array(5)].map((_, i) => (
                  <TableRow key={i}>
                    <TableCell colSpan={8}>
                      <Skeleton className="h-6 w-full" />
                    </TableCell>
                  </TableRow>
                ))
              ) : readings.length > 0 ? (
                readings.map((rec) => (
                  <TableRow
                    key={rec.hash}
                    onClick={() => setSelectedHash(rec.hash)}
                    className={`cursor-pointer ${
                      !rec.is_valid_hash || !rec.is_valid_chain ? 'bg-brick-subtle/40' : ''
                    }`}
                  >
                    <TableCell className="font-semibold text-ink">
                      {rec.timestamp}
                    </TableCell>
                    <TableCell>{rec.gas_ppm.toFixed(3)}</TableCell>
                    <TableCell>{rec.power_mW.toFixed(1)}</TableCell>
                    <TableCell>
                      <Badge
                        variant={rec.plausibility === 'SUSPICIOUS' ? 'critical' : 'neutral'}
                        size="sm"
                      >
                        {rec.plausibility}
                      </Badge>
                    </TableCell>
                    <TableCell>
                      <Badge
                        variant={rec.fingerprint_status === 'SENSOR_OK' ? 'verified' : 'critical'}
                        size="sm"
                      >
                        {rec.fingerprint_status}
                      </Badge>
                    </TableCell>
                    <TableCell className="font-mono text-[11px] text-muted">
                      {rec.hash.substring(0, 10)}...{rec.hash.substring(58)}
                    </TableCell>
                    <TableCell>
                      {rec.is_valid_hash ? (
                        <span className="text-teal font-medium">VALID</span>
                      ) : (
                        <span className="text-brick font-bold">TAMPERED</span>
                      )}
                    </TableCell>
                    <TableCell>
                      {rec.is_valid_chain ? (
                        <span className="text-teal font-medium">LINKED</span>
                      ) : (
                        <span className="text-brick font-bold">BROKEN</span>
                      )}
                    </TableCell>
                  </TableRow>
                ))
              ) : (
                <TableRow>
                  <TableCell colSpan={8} className="text-center py-6 text-muted">
                    No records loaded.
                  </TableCell>
                </TableRow>
              )}
            </TableBody>
          </Table>
        </CardContent>
      </Card>
    </div>
  );
}
