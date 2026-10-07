import { useState } from 'react';
import { useQuery, useMutation } from '@tanstack/react-query';
import { api } from '../api/client';
import { Card, CardHeader, CardContent } from '../components/ui/Card';
import { Badge } from '../components/ui/Badge';
import { Button, Skeleton } from '../components/ui/Primitives';

export function MLAdvisoryPage() {
  const [gasInput, setGasInput] = useState<number>(2.45);
  const [powerInput, setPowerInput] = useState<number>(10.0);

  const {
    data: mlStatus,
    isLoading: isStatusLoading,
  } = useQuery({
    queryKey: ['mlStatus'],
    queryFn: () => api.getMLStatus(),
  });

  const predictMutation = useMutation({
    mutationFn: (payload: { gas_ppm: number; power_mW: number }) =>
      api.predictML(payload),
  });

  const prediction = predictMutation.data;

  return (
    <div className="space-y-6">
      {/* Prominent advisory disclaimer banner */}
      <div className="p-4 border border-ochre/40 bg-ochre-subtle text-ochre font-mono-num">
        <div className="flex items-center gap-2">
          <span className="w-2 h-2 rounded-full bg-ochre" />
          <h3 className="text-xs font-bold uppercase tracking-wider">
            Advisory Notice — Deterministic Layers Are Authoritative
          </h3>
        </div>
        <p className="text-xs mt-1 text-ochre/90 leading-relaxed">
          Machine Learning models in TrueSense serve strictly as exploratory classifiers for anomaly trend detection.
          All regulatory compliance verdicts, tamper evaluations, and log integrity assertions remain strictly governed
          by the deterministic Layer 1 (Physical Plausibility), Layer 2 (Sensor Fingerprint), and Layer 3 (SHA-256 Ledger).
        </p>
      </div>

      {/* Model Meta / Status Card */}
      <Card>
        <CardHeader
          title="ACTIVE ML MODEL SPECIFICATION"
          description="Model weights and training provenance registered under /api/v1/ml/status."
          action={
            <Badge variant="neutral" size="sm">
              ADVISORY ONLY
            </Badge>
          }
        />
        <CardContent>
          {isStatusLoading ? (
            <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
              <Skeleton className="h-14 w-full" />
              <Skeleton className="h-14 w-full" />
              <Skeleton className="h-14 w-full" />
              <Skeleton className="h-14 w-full" />
            </div>
          ) : mlStatus ? (
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4 font-mono-num text-xs">
              <div className="p-3 border border-hairline bg-surface">
                <span className="text-[11px] text-muted block uppercase">Model Version</span>
                <span className="text-sm font-semibold text-ink">{mlStatus.model_version}</span>
              </div>
              <div className="p-3 border border-hairline bg-surface">
                <span className="text-[11px] text-muted block uppercase">Benchmark Accuracy</span>
                <span className="text-sm font-semibold text-ink">
                  {(mlStatus.accuracy * 100).toFixed(1)}%
                </span>
              </div>
              <div className="p-3 border border-hairline bg-surface">
                <span className="text-[11px] text-muted block uppercase">Operational State</span>
                <div className="mt-0.5">
                  <Badge variant="verified" size="sm">{mlStatus.status}</Badge>
                </div>
              </div>
              <div className="p-3 border border-hairline bg-surface">
                <span className="text-[11px] text-muted block uppercase">Training Corpus</span>
                <span className="text-[11px] text-muted truncate block">{mlStatus.trained_on}</span>
              </div>
            </div>
          ) : null}
        </CardContent>
      </Card>

      {/* Interactive Predict Evaluation Panel */}
      <Card>
        <CardHeader
          title="INFERENCE BENCHMARK & DISAGREEMENT EVALUATION"
          description="Simulate dual evaluation: deterministic rules vs exploratory statistical classifier."
        />
        <CardContent className="space-y-6">
          {/* Input Parameter Sliders */}
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-6 p-4 border border-hairline bg-surface">
            <div className="space-y-2">
              <div className="flex justify-between text-xs font-mono-num">
                <label htmlFor="gas-voltage-input" className="font-semibold text-ink">Gas Signal (Proxy Voltage)</label>
                <span className="text-muted">{gasInput.toFixed(3)} V</span>
              </div>
              <input
                id="gas-voltage-input"
                aria-label="Gas Signal (Proxy Voltage)"
                type="range"
                min="0.5"
                max="4.0"
                step="0.05"
                value={gasInput}
                onChange={(e) => setGasInput(parseFloat(e.target.value))}
                className="w-full accent-teal cursor-pointer"
              />
              <span className="text-[11px] text-muted block">
                MQ-135 load resistor output (baseline ~1.0V, high spike &gt; 2.0V)
              </span>
            </div>

            <div className="space-y-2">
              <div className="flex justify-between text-xs font-mono-num">
                <label htmlFor="equipment-power-input" className="font-semibold text-ink">Equipment Power Draw</label>
                <span className="text-muted">{powerInput.toFixed(1)} mW</span>
              </div>
              <input
                id="equipment-power-input"
                aria-label="Equipment Power Draw"
                type="range"
                min="0"
                max="1000"
                step="10"
                value={powerInput}
                onChange={(e) => setPowerInput(parseFloat(e.target.value))}
                className="w-full accent-teal cursor-pointer"
              />
              <span className="text-[11px] text-muted block">
                INA219 current monitor (active threshold = 50.0 mW)
              </span>
            </div>
          </div>

          <div className="flex justify-end">
            <Button
              variant="primary"
              isLoading={predictMutation.isPending}
              onClick={() =>
                predictMutation.mutate({
                  gas_ppm: gasInput,
                  power_mW: powerInput,
                })
              }
            >
              Run Dual Inference Check
            </Button>
          </div>

          {/* Results Comparison View */}
          {prediction && (
            <div className="border border-hairline p-4 bg-surface space-y-4 font-mono-num">
              <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                {/* Authoritative Deterministic Verdict */}
                <div className="border border-hairline p-3 bg-surface-elevated">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-[11px] text-muted uppercase font-bold">
                      Deterministic Verdict (AUTHORITATIVE)
                    </span>
                    <Badge
                      variant={
                        prediction.deterministic_verdict === 'SUSPICIOUS'
                          ? 'critical'
                          : 'verified'
                      }
                      size="sm"
                    >
                      {prediction.deterministic_verdict}
                    </Badge>
                  </div>
                  <p className="text-xs text-muted leading-relaxed">
                    Evaluated by firmware Layer 1 rules (gas threshold vs 90s power history).
                  </p>
                </div>

                {/* Advisory ML Verdict */}
                <div className="border border-hairline p-3 bg-surface-elevated">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-[11px] text-muted uppercase font-bold">
                      ML Classifier (ADVISORY)
                    </span>
                    <Badge
                      variant={
                        prediction.ml_verdict === 'SUSPICIOUS'
                          ? 'warning'
                          : 'neutral'
                      }
                      size="sm"
                    >
                      {prediction.ml_verdict}
                    </Badge>
                  </div>
                  <p className="text-xs text-muted leading-relaxed">
                    Confidence: {(prediction.ml_confidence * 100).toFixed(1)}%. Model inference over feature drift and ratio.
                  </p>
                </div>
              </div>

              {/* Disagreement Evaluation Banner */}
              <div
                className={`p-3 border text-xs ${
                  prediction.disagreement
                    ? 'border-ochre bg-ochre-subtle text-ochre'
                    : 'border-teal bg-teal-subtle text-teal'
                }`}
              >
                <div className="font-bold">
                  {prediction.disagreement
                    ? '⚠️ CLASSIFIER DISAGREEMENT DETECTED'
                    : '✓ VERDICTS IN ALIGNMENT'}
                </div>
                <div className="mt-1 opacity-90">{prediction.reason}</div>
              </div>
            </div>
          )}
        </CardContent>
      </Card>
    </div>
  );
}
