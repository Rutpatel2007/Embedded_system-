import { Card, CardHeader, CardContent } from '../components/ui/Card';
import { Table, TableHeader, TableBody, TableRow, TableHead, TableCell } from '../components/ui/Table';

export function ArchitecturePage() {
  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="border-b border-hairline pb-4">
        <h2 className="text-xl font-semibold tracking-tight text-ink font-mono-num">
          SYSTEM ARCHITECTURE & FIRMWARE PIPELINE
        </h2>
        <p className="text-xs text-muted mt-0.5">
          Detailed technical reference for auditors and reviewers: end-to-end telemetry lifecycle from analog acquisition to cryptographic verification.
        </p>
      </div>

      {/* Firmware Pipeline Flow */}
      <Card>
        <CardHeader
          title="FIRMWARE EXECUTION & SYNCHRONIZATION PIPELINE"
          description="High-frequency deterministic pipeline running on ESP32 dual Xtensa LX6 cores."
        />
        <CardContent>
          <div className="grid grid-cols-1 md:grid-cols-4 gap-3 text-xs font-mono">
            <div className="p-3 border border-hairline bg-surface">
              <span className="text-[10px] text-teal font-bold uppercase block mb-1">01. Acquisition</span>
              <div className="font-semibold text-ink">MQ-135 + INA219</div>
              <p className="text-[11px] text-muted mt-1 leading-snug">
                500ms deterministic periodic tick via FreeRTOS vTaskDelayUntil pinned to Core 1.
              </p>
            </div>

            <div className="p-3 border border-hairline bg-surface">
              <span className="text-[10px] text-teal font-bold uppercase block mb-1">02. Ring Buffers</span>
              <div className="font-semibold text-ink">Zero-Heap Buffers</div>
              <p className="text-[11px] text-muted mt-1 leading-snug">
                120-slot Gas buffer (60s) and 180-slot Power buffer (90s) in static memory.
              </p>
            </div>

            <div className="p-3 border border-hairline bg-surface">
              <span className="text-[10px] text-teal font-bold uppercase block mb-1">03. Dual Layer Check</span>
              <div className="font-semibold text-ink">Layer 1 & Layer 2</div>
              <p className="text-[11px] text-muted mt-1 leading-snug">
                Layer 1 cross-modal check + Layer 2 warm-up curve cosine similarity match against NVS profile.
              </p>
            </div>

            <div className="p-3 border border-hairline bg-surface">
              <span className="text-[10px] text-teal font-bold uppercase block mb-1">04. Ledger Commit</span>
              <div className="font-semibold text-ink">SHA-256 Hash Chain</div>
              <p className="text-[11px] text-muted mt-1 leading-snug">
                Canonical string serialized; hashed via mbedTLS; written to microSD log.jsonl.
              </p>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Canonical Serialization Format */}
      <Card>
        <CardHeader
          title="CANONICAL SERIALIZATION FORMAT"
          description="Exact deterministic string structure used for SHA-256 hash generation across C++ and Python."
        />
        <CardContent className="space-y-3 font-mono text-xs">
          <div className="p-3 bg-surface border border-hairline text-ink break-all font-mono-num font-semibold">
            device_id|timestamp|gas_ppm|power_mW|plausibility|fingerprint_status|previous_hash
          </div>

          <div className="space-y-1 text-muted text-xs leading-relaxed">
            <p><strong className="text-ink">Delimiter:</strong> ASCII 0x7C (<code className="bg-surface px-1 py-0.5 border border-hairline">|</code>). No spaces, no newlines.</p>
            <p><strong className="text-ink">Floats:</strong> Formatted to exactly 3 decimal places (<code className="bg-surface px-1 py-0.5 border border-hairline">%.3f</code>) e.g., <code className="bg-surface px-1 py-0.5 border border-hairline">42.381</code>.</p>
            <p><strong className="text-ink">Genesis Previous Hash:</strong> Exactly 64 ASCII zeros (<code className="bg-surface px-1 py-0.5 border border-hairline">0000000000000000000000000000000000000000000000000000000000000000</code>).</p>
          </div>
        </CardContent>
      </Card>

      {/* Task, Core, Priority Table */}
      <Card>
        <CardHeader
          title="FREERTOS TASK & RESOURCE ALLOCATION"
          description="Task priority assignment and core affinity guarantees for bounded real-time execution."
        />
        <CardContent className="p-0">
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>TASK NAME</TableHead>
                <TableHead>CORE AFFINITY</TableHead>
                <TableHead>PRIORITY</TableHead>
                <TableHead>STACK SIZE</TableHead>
                <TableHead>INTERVAL / TRIGGER</TableHead>
                <TableHead>PURPOSE</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              <TableRow>
                <TableCell className="font-semibold text-ink">SensorTask</TableCell>
                <TableCell>Core 1</TableCell>
                <TableCell>Priority 3 (High)</TableCell>
                <TableCell>4096 B</TableCell>
                <TableCell>500 ms (Periodic)</TableCell>
                <TableCell className="text-muted">ADC reading, INA219 query, jitter timestamping</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="font-semibold text-ink">AlgorithmTask</TableCell>
                <TableCell>Core 0</TableCell>
                <TableCell>Priority 2 (Normal)</TableCell>
                <TableCell>4096 B</TableCell>
                <TableCell>Queue-driven (FIFO)</TableCell>
                <TableCell className="text-muted">Ring buffer push, Layer 1 plausibility, Layer 2 fingerprinting</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="font-semibold text-ink">HealthTask</TableCell>
                <TableCell>Core 0</TableCell>
                <TableCell>Priority 1 (Low)</TableCell>
                <TableCell>2048 B</TableCell>
                <TableCell>5000 ms (Periodic)</TableCell>
                <TableCell className="text-muted">Heap watermark, queue drop counter, jitter moving average</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="font-semibold text-ink">NetworkTask</TableCell>
                <TableCell>Core 0</TableCell>
                <TableCell>Priority 1 (Low)</TableCell>
                <TableCell>8192 B</TableCell>
                <TableCell>Async WiFi Sync</TableCell>
                <TableCell className="text-muted">Batch upload of JSONL records to FastAPI /api/v1/readings/ingest</TableCell>
              </TableRow>
            </TableBody>
          </Table>
        </CardContent>
      </Card>

      {/* Tech Stack Summary */}
      <Card>
        <CardHeader
          title="UNIFIED MONOREPO TECH STACK"
          description="Integrated toolchains spanning firmware, cloud backend, and web dashboard."
        />
        <CardContent>
          <div className="grid grid-cols-1 sm:grid-cols-3 gap-4 text-xs font-mono">
            <div className="p-3 border border-hairline bg-surface">
              <span className="font-bold text-ink uppercase block mb-1">Firmware</span>
              <ul className="text-muted space-y-1">
                <li>• ESP-IDF / Arduino on ESP32</li>
                <li>• PlatformIO toolchain</li>
                <li>• FreeRTOS dual-core tasks</li>
                <li>• mbedTLS SHA-256 hardware accel</li>
              </ul>
            </div>

            <div className="p-3 border border-hairline bg-surface">
              <span className="font-bold text-ink uppercase block mb-1">Backend</span>
              <ul className="text-muted space-y-1">
                <li>• FastAPI + Uvicorn</li>
                <li>• SQLAlchemy + SQLite / Postgres</li>
                <li>• Pydantic v2 schemas</li>
                <li>• Pytest test suite (63+ tests)</li>
              </ul>
            </div>

            <div className="p-3 border border-hairline bg-surface">
              <span className="font-bold text-ink uppercase block mb-1">Frontend Dashboard</span>
              <ul className="text-muted space-y-1">
                <li>• React 19 + TypeScript + Vite 8</li>
                <li>• TanStack Query v5</li>
                <li>• Recharts + Tailwind CSS v4</li>
                <li>• Vitest unit testing suite</li>
              </ul>
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
