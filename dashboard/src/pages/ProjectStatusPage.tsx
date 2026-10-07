import { Card, CardHeader, CardContent } from '../components/ui/Card';
import { Badge } from '../components/ui/Badge';
import { Table, TableHeader, TableBody, TableRow, TableHead, TableCell } from '../components/ui/Table';

interface CapabilityItem {
  component: string;
  subsystem: string;
  status: 'IMPLEMENTED' | 'SOFTWARE_TESTED' | 'SIMULATED' | 'HARDWARE_VALIDATED' | 'UNVALIDATED';
  details: string;
}

const CAPABILITIES: CapabilityItem[] = [
  {
    component: 'Hardware Drivers (MQ-135, INA219, DS3231, SD, Relay)',
    subsystem: 'Firmware HAL',
    status: 'IMPLEMENTED',
    details: 'Hardware drivers implemented and integrated via PlatformIO.',
  },
  {
    component: 'Sensor Provider Abstraction (ISensorProvider)',
    subsystem: 'Firmware Core',
    status: 'IMPLEMENTED',
    details: 'Decoupled sensor interface supporting both physical hardware and simulation harness.',
  },
  {
    component: 'Mock Simulation Dataset Replay',
    subsystem: 'Firmware Simulation',
    status: 'SIMULATED',
    details: '600-sample deterministic CSV replay executing 5 operational anomaly scenarios.',
  },
  {
    component: '500ms Deterministic Scheduler (SensorTask)',
    subsystem: 'FreeRTOS / Core 1',
    status: 'SOFTWARE_TESTED',
    details: 'Pinned task utilizing vTaskDelayUntil with moving average jitter tracking.',
  },
  {
    component: 'Bounded Zero-Heap Ring Buffers',
    subsystem: 'Firmware Memory',
    status: 'SOFTWARE_TESTED',
    details: '60s gas history (120 slots) and 90s power history (180 slots) with static allocation.',
  },
  {
    component: 'Layer 1 Physical Plausibility Engine',
    subsystem: 'Verification Engine',
    status: 'SOFTWARE_TESTED',
    details: 'Cross-modal gas spike vs trailing power window evaluation, tested with unit suites.',
  },
  {
    component: 'Layer 2 Sensor Identity Fingerprint',
    subsystem: 'Silicon Integrity',
    status: 'SOFTWARE_TESTED',
    details: 'Cosine similarity of 20-point warm-up profile matching against NVS reference.',
  },
  {
    component: 'Cryptographic SHA-256 Ledger & Genesis Rule',
    subsystem: 'Ledger Engine',
    status: 'SOFTWARE_TESTED',
    details: 'Canonical serialization string protocol matching between mbedTLS and Python hashlib.',
  },
  {
    component: 'FastAPI Backend Endpoints (/api/v1/*)',
    subsystem: 'Cloud Service',
    status: 'SOFTWARE_TESTED',
    details: 'Full contract endpoints verified with 63 passing pytest test cases.',
  },
  {
    component: 'MQ-135 Load Resistor RL & PPM Calibration',
    subsystem: 'Analog Hardware',
    status: 'UNVALIDATED',
    details: 'Sensor resistance ratio and PPM conversion curves pending physical lab gas chamber calibration.',
  },
  {
    component: 'Spike & Plausibility Power Thresholds (50mW)',
    subsystem: 'Algorithm Parameters',
    status: 'UNVALIDATED',
    details: 'Configurable starting heuristics; requires multi-week field deployment measurements.',
  },
  {
    component: 'Production Hardware Cryptographic Keystore',
    subsystem: 'Security Architecture',
    status: 'UNVALIDATED',
    details: 'Firmware currently uses development admin credential strings; requires hardware secure element.',
  },
];

export function ProjectStatusPage() {
  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="border-b border-hairline pb-4">
        <h2 className="text-xl font-semibold tracking-tight text-ink font-mono-num">
          HONEST CAPABILITY & READINESS MATRIX
        </h2>
        <p className="text-xs text-muted mt-0.5">
          Audited status of every architectural subsystem. TrueSense strictly distinguishes simulated software prototypes from physical hardware validation.
        </p>
      </div>

      {/* Honesty Legend */}
      <Card>
        <CardHeader
          title="VALIDATION STATUS TAXONOMY"
          description="Clear boundaries between simulation validation and physical field readiness."
        />
        <CardContent>
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-3 text-xs font-mono">
            <div className="p-3 border border-hairline bg-surface">
              <Badge variant="verified" size="sm">SOFTWARE_TESTED</Badge>
              <p className="text-[11px] text-muted mt-1.5 leading-snug">
                Unit test suites passing in simulation harness and CI/CD pipelines.
              </p>
            </div>

            <div className="p-3 border border-hairline bg-surface">
              <Badge variant="warning" size="sm">SIMULATED</Badge>
              <p className="text-[11px] text-muted mt-1.5 leading-snug">
                Demonstrated using synthetic sensor datasets or mock sensor providers.
              </p>
            </div>

            <div className="p-3 border border-hairline bg-surface">
              <Badge variant="neutral" size="sm">IMPLEMENTED</Badge>
              <p className="text-[11px] text-muted mt-1.5 leading-snug">
                Code written and compiled, pending end-to-end integration test execution.
              </p>
            </div>

            <div className="p-3 border border-hairline bg-surface">
              <Badge variant="critical" size="sm">UNVALIDATED</Badge>
              <p className="text-[11px] text-muted mt-1.5 leading-snug">
                Physical sensor calibration or field deployment empirical tuning not yet performed.
              </p>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Comprehensive Capability Table */}
      <Card>
        <CardHeader
          title="SUBSYSTEM CAPABILITY MATRIX"
          description="Directly compiled from backend contracts, firmware specifications, and engineering audit."
        />
        <CardContent className="p-0">
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>SUBSYSTEM</TableHead>
                <TableHead>COMPONENT</TableHead>
                <TableHead>VALIDATION STATUS</TableHead>
                <TableHead>ENGINEERING AUDIT DETAILS</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {CAPABILITIES.map((cap, i) => (
                <TableRow key={i}>
                  <TableCell className="font-semibold text-ink text-[11px]">
                    {cap.subsystem}
                  </TableCell>
                  <TableCell className="text-ink text-[11px] font-medium">
                    {cap.component}
                  </TableCell>
                  <TableCell>
                    <Badge
                      variant={
                        cap.status === 'SOFTWARE_TESTED'
                          ? 'verified'
                          : cap.status === 'SIMULATED'
                          ? 'warning'
                          : cap.status === 'UNVALIDATED'
                          ? 'critical'
                          : 'neutral'
                      }
                      size="sm"
                    >
                      {cap.status}
                    </Badge>
                  </TableCell>
                  <TableCell className="text-muted text-[11px] leading-snug">
                    {cap.details}
                  </TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </CardContent>
      </Card>
    </div>
  );
}
