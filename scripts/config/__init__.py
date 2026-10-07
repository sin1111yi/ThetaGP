"""
ThetaGP board config library — the table a board's declaration is read
against, and the walks that hold it to that table and turn it into the board's
header and CMake variables.
"""

from .engine import Emission, Table, emit, validate
from .output import assemble_cmake, assemble_header
from .schema import BOARD_SCHEMA

__all__ = [
    "BOARD_SCHEMA",
    "Emission",
    "Table",
    "emit",
    "validate",
    "assemble_cmake",
    "assemble_header",
]
