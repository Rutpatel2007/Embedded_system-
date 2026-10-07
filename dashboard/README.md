# TrueSense (PROOV) Web Dashboard

The web dashboard for **TrueSense (PROOV)**, a tamper-evident ESP32 emissions-monitoring system that binds uncalibrated gas proxy telemetry to industrial equipment electrical power states and commits every reading into a verifiable SHA-256 hash log.

Designed with a calm, editorial, technical-instrument interface:
- **Palette**: Warm off-white background (`#F6F4EF`), near-black ink (`#16181A`), hairline graphite borders (`#E2DFD7`), and deep teal accent (`#0F5C5A`). Muted alerts (warning ochre `#B7791F`, critical brick `#A63A2B`).
- **Typography**: Clean sans UI with tabular monospace numerals (`font-mono-num`) for hashes, timestamps, and readings.
- **Honesty Guarantees**: Gas values are explicitly labelled `Gas signal (V, uncalibrated proxy)`, never fabricated as true PPM. Persistent simulation disclaimer banner is present on all views.

---

## 1. Quick Start in 3 Commands

### Step 1: Start Backend Server
```bash
uvicorn app.main:app --host 127.0.0.1 --port 8000 --app-dir backend
```

### Step 2: Seed Simulation Data (600 Records)
```bash
python scripts/seed_demo.py
```

### Step 3: Run the Dashboard
```bash
cd dashboard
npm run dev
```
Open [http://localhost:5173](http://localhost:5173) in your browser.

---

## 2. Environment Variables

Create `.env` in `/dashboard` (or use `.env.example` defaults):
```bash
# FastAPI Backend Base URL (Vite development proxy handles /api to http://127.0.0.1:8000)
VITE_API_BASE_URL=http://127.0.0.1:8000
```

---

## 3. Pages & Features

| Page | URL Path | Description & API Wiring |
| :--- | :--- | :--- |
| **Overview** | `/` | Thesis statement, live backend health badge, ingest freshness epoch tracker, overall chain integrity badge, and the 3-Layer Trust Chain visual architecture diagram. |
| **Live Monitor** | `/live` | Time-aligned dual charts for gas sensor proxy voltage (`MQ-135`) and equipment power draw (`INA219`), range selector (50, 100, 200, 500 points), polling toggle (3s), and latest packet status cards. |
| **Integrity** | `/integrity` | One-click full ledger walk trigger (`/verify/{device_id}`), visual linked block inspector (`H(n-1) -> H(n)`), and comprehensive per-record audit table. |
| **Alerts** | `/alerts` | Filterable table by severity (`CRITICAL`, `HIGH`, etc.) and event type, with details drawer. Displays physical plausibility violations, sensor replacement alarms, and data tampering events. |
| **ML Advisory** | `/ml-advisory` | Model metadata, exploratory parameters sliders, and side-by-side comparison between authoritative deterministic firmware verdict and advisory ML classifier. |
| **Architecture** | `/architecture` | Complete technical reference for auditors: 4-stage firmware pipeline, pipe-delimited canonical serialization specification, and FreeRTOS task affinity table. |
| **Project Status** | `/status` | Audited capability matrix honestly classifying every subsystem as `SOFTWARE_TESTED`, `SIMULATED`, `IMPLEMENTED`, or `UNVALIDATED`. |

---

## 4. Demonstrating Tamper-Evident Integrity

TrueSense includes a zero-backend-modification tamper tool for audit verification:

1. **Verify Authentic Chain**:
   - In the dashboard, navigate to **Integrity** and click **Verify Entire Chain**.
   - Result: **VERIFIED (0 FAULTS, 600 records intact)**.
2. **Simulate Insider Tampering**:
   - Run:
     ```bash
     python scripts/demo_tamper.py
     ```
   - This directly alters a database record's gas voltage without recomputing the SHA-256 hash.
3. **Verify Tampered Chain**:
   - Return to **Integrity** and click **Verify Entire Chain**.
   - Result: **INTEGRITY BREACH DETECTED (Tampered: 1, Record #25 flagged in red)**.
4. **Restore Authentic State**:
   - Run:
     ```bash
     python scripts/demo_tamper.py --restore
     ```
   - Re-run verification in dashboard: **VERIFIED (Chain intact)**.

---

## 5. Backend Gaps & Frontend Accommodations

During the audit of `/api/v1`, the following items were identified and cleanly accommodated:

1. **No Device List Endpoint (`/devices`)**:
   - Defaulted primary node identifier to `TS001` (the enrolled factory test node).
2. **No Backend Machine Learning Endpoints (`/api/v1/ml/status`, `/api/v1/ml/predict`) on backend**:
   - Implemented a graceful client layer in `src/api/client.ts` that attempts the real backend routes first, falling back to local deterministic rule evaluation and advisory inference if the endpoint is absent.
3. **Array vs Scalar Discrepancy in Documentation**:
   - The markdown documentation referenced `tampered_records: []` array, but the authoritative FastAPI backend schemas and SQLAlchemy implementation return integer counts `tampered_records: int` and `chain_breaks: int`. The frontend client strictly aligns with the real Pydantic response contract.
