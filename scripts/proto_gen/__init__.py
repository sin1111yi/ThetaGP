"""
The protocol code generator, split by responsibility.

scripts/gen_proto.py stays where it is — the entry point every caller names —
and holds the CLI and the call order. Everything the run is made of lives here:
model.py reads protocol.toml, validate.py stops on a protocol the artifacts
cannot describe, and one module per artifact class emits. The split is
mechanical: no function was renamed, no message reworded, and the artifacts are
byte-identical to the ones the single-file generator wrote.

Each module here is part of the digest recorded in protocol/proto_fields.json
(GENERATOR_SOURCES in model.py), so a change to any of them makes a stale
manifest detectable rather than silently authoritative.
"""
