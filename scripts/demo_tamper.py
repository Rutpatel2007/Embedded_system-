"""
Demo Tamper Script for TrueSense.
Simulates an unauthorized insider modification directly in the demo SQLite DB without modifying backend logic.
Also supports --restore to revert the database back to its authentic state.

Usage:
    python scripts/demo_tamper.py [--db path/to/truesense_test.db] [--record-id 42]
    python scripts/demo_tamper.py --restore [--db path/to/truesense_test.db]
"""

import argparse
import os
import sqlite3
import sys

DEFAULT_DB_LOCATIONS = [
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "truesense_test.db"),
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "backend", "truesense_test.db"),
]

BACKUP_TABLE_NAME = "_reading_tamper_backup"


def find_db_path(specified_path: str = None) -> str:
    if specified_path:
        return specified_path
    for path in DEFAULT_DB_LOCATIONS:
        if os.path.exists(path):
            return path
    # Default to backend/truesense_test.db
    return DEFAULT_DB_LOCATIONS[0]


def tamper(db_path: str, record_id: int = None, target_gas: float = 9.999):
    if not os.path.exists(db_path):
        print(f"[-] SQLite database not found at {db_path}.")
        print("    Please start backend and run scripts/seed_demo.py first.")
        sys.exit(1)

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    # Create backup table if not exists
    cur.execute(
        f"""
        CREATE TABLE IF NOT EXISTS {BACKUP_TABLE_NAME} (
            id INTEGER PRIMARY KEY,
            gas_ppm REAL,
            original_hash TEXT
        )
    """
    )

    if record_id is None:
        # Default to record 25 if available, else middle record
        cur.execute("SELECT id, gas_ppm, hash FROM readings ORDER BY id ASC")
        rows = cur.fetchall()
        if not rows:
            print("[-] No readings found in database to tamper with.")
            conn.close()
            sys.exit(1)
        target_row = rows[min(24, len(rows) - 1)]
        record_id = target_row[0]
        original_gas = target_row[1]
        original_hash = target_row[2]
    else:
        cur.execute("SELECT id, gas_ppm, hash FROM readings WHERE id = ?", (record_id,))
        row = cur.fetchone()
        if not row:
            print(f"[-] Record ID {record_id} not found in database.")
            conn.close()
            sys.exit(1)
        original_gas = row[1]
        original_hash = row[2]

    # Save to backup table
    cur.execute(
        f"INSERT OR REPLACE INTO {BACKUP_TABLE_NAME} (id, gas_ppm, original_hash) VALUES (?, ?, ?)",
        (record_id, original_gas, original_hash),
    )

    # Perform unauthorized modification: alter gas_ppm without recomputing hash
    cur.execute(
        "UPDATE readings SET gas_ppm = ? WHERE id = ?",
        (target_gas, record_id),
    )
    conn.commit()
    conn.close()

    print("[!] TAMPER COMPLETED on Demo SQLite Database.")
    print(f"    Record ID: {record_id}")
    print(f"    Gas Signal modified: {original_gas} -> {target_gas} V (Proxy)")
    print(f"    Hash left unchanged: {original_hash}")
    print(f"    On the next /verify/TS001 call, verification WILL FAIL at Record #{record_id}.")
    print("    To revert, run: python scripts/demo_tamper.py --restore")


def restore(db_path: str):
    if not os.path.exists(db_path):
        print(f"[-] SQLite database not found at {db_path}.")
        sys.exit(1)

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()

    cur.execute(
        f"SELECT name FROM sqlite_master WHERE type='table' AND name='{BACKUP_TABLE_NAME}'"
    )
    if not cur.fetchone():
        print("[-] No tamper backup found. Nothing to restore.")
        conn.close()
        return

    cur.execute(f"SELECT id, gas_ppm FROM {BACKUP_TABLE_NAME}")
    backups = cur.fetchall()

    for rec_id, original_gas in backups:
        cur.execute("UPDATE readings SET gas_ppm = ? WHERE id = ?", (original_gas, rec_id))
        print(f"[SUCCESS] Restored Record #{rec_id} gas_ppm to authentic value: {original_gas}")

    cur.execute(f"DROP TABLE {BACKUP_TABLE_NAME}")
    conn.commit()
    conn.close()
    print("[SUCCESS] All tampered records restored. Chain integrity is now intact.")


def main():
    parser = argparse.ArgumentParser(description="Simulate insider tampering or restore authentic ledger state.")
    parser.add_argument("--db", default=None, help="Path to SQLite database file")
    parser.add_argument("--record-id", type=int, default=None, help="Record ID to tamper with")
    parser.add_argument("--gas", type=float, default=9.999, help="Tampered gas signal value (default: 9.999)")
    parser.add_argument("--restore", action="store_true", help="Restore tampered database to authentic state")

    args = parser.parse_args()
    db_path = find_db_path(args.db)

    if args.restore:
        restore(db_path)
    else:
        tamper(db_path, args.record_id, args.gas)


if __name__ == "__main__":
    main()
