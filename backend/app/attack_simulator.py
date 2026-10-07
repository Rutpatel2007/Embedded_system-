from decimal import Decimal
from sqlalchemy.orm import Session
from app.models import Reading

class AttackSimulator:
    @staticmethod
    def simulate_gas_value_tampering(db: Session, device_id: str, new_gas_value: Decimal, offset: int = 0):
        """A. GAS_VALUE_TAMPERING: Modifies the gas_ppm of a reading without recalculating the hash."""
        record = db.query(Reading).filter(Reading.device_id == device_id).order_by(Reading.id.asc()).offset(offset).first()
        if record:
            record.gas_ppm = new_gas_value
            db.commit()
        return record

    @staticmethod
    def simulate_power_value_tampering(db: Session, device_id: str, new_power_value: Decimal, offset: int = 0):
        """B. POWER_VALUE_TAMPERING: Modifies the power_mW of a reading without recalculating the hash."""
        record = db.query(Reading).filter(Reading.device_id == device_id).order_by(Reading.id.asc()).offset(offset).first()
        if record:
            record.power_mW = new_power_value
            db.commit()
        return record

    @staticmethod
    def simulate_hash_tampering(db: Session, device_id: str, new_hash: str, offset: int = 0):
        """C. HASH_TAMPERING: Modifies the stored hash of a reading."""
        record = db.query(Reading).filter(Reading.device_id == device_id).order_by(Reading.id.asc()).offset(offset).first()
        if record:
            record.hash = new_hash
            db.commit()
        return record

    @staticmethod
    def simulate_previous_hash_break(db: Session, device_id: str, new_prev_hash: str, offset: int = 1):
        """D. PREVIOUS_HASH_BREAK: Modifies the previous_hash of a reading to break the chain."""
        record = db.query(Reading).filter(Reading.device_id == device_id).order_by(Reading.id.asc()).offset(offset).first()
        if record:
            record.previous_hash = new_prev_hash
            db.commit()
        return record

    @staticmethod
    def simulate_first_record_non_genesis(db: Session, device_id: str, new_prev_hash: str = "1" * 64):
        """E. FIRST_RECORD_NON_GENESIS: Modifies the previous_hash of the first reading to something other than the genesis hash."""
        return AttackSimulator.simulate_previous_hash_break(db, device_id, new_prev_hash, offset=0)
