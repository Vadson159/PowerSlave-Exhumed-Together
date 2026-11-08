"""P4 read-only historical/game/save audit, using the baseline made before this package."""
from pathlib import Path
import integrity as gate
gate.OUT=gate.ROOT/'docs/evidence/m1/PSE_TOGETHER_P4'
gate.OWN_DOC='docs/65_P4_PRESENTATION_AND_SPAWN.md'
if __name__=='__main__':
    raise SystemExit(0 if gate.check('INTEGRITY_FINAL') else 1)
