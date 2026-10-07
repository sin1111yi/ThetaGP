#!/usr/bin/env python3

import argparse
import glob
import hashlib
import json
import os
import os.path
import re
import sys
import time
from typing import NoReturn

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

BINDINGS_DIR_NAME = "proto_py"

def default_bindings_dir():
    """Where the configure wrote the host bindings.

    The configure writes them into the build directory it was given, and build/
    is the one this project is configured into; a build directory of another
    name carries one of its own. The newest of them is the answer, because the
    build configured last wrote its bindings last, and a reply has to be read by
    the schema that build was built against.
    """
    written = [os.path.join(directory, "ThetaGP_pb2.py")
               for directory in glob.glob(os.path.join(REPO_ROOT, "build*",
                                                       BINDINGS_DIR_NAME))]
    written = [path for path in written if os.path.isfile(path)]
    if not written:
        return os.path.join(REPO_ROOT, "build", BINDINGS_DIR_NAME)
    return os.path.dirname(max(written, key=os.path.getmtime))

DEFAULT_BINDINGS_DIR = default_bindings_dir()

FRAME_PAYLOAD_MAX = 1024        # ADR-0007 section 3.5, FRAME_PAYLOAD_MAX
FRAME_PREFIX_MAX_BYTES = 2
FRAME_CHECKSUM_BYTES = 2        # ADR-0007 section 3.1: trailing, little endian
DEFAULT_IDLE_TIMEOUT_S = 1.0    # ADR-0007 section 3.4 rule 5
DEFAULT_BAUD = 115200           # CDC ACM carries its own rate; kept for the port call
PING_TIMEOUT_S = 2.0
PING_ATTEMPTS = 3               # section 3.4 rule 6: flush and resend on TransportError

EXIT_OK = 0
EXIT_FRAME = 1
EXIT_USAGE = 2
EXIT_DEPENDENCY = 3
EXIT_IO = 5

PROTOBUF_PYTHONPATH_ENV = "PROTOBUF_PYTHONPATH"
SUGGESTED_VENV = os.path.join(os.path.expanduser("~"), ".venvs", "thetagp-tools")

def eprint(*parts):
    print(*parts, file=sys.stderr)

def fail(message, code) -> NoReturn:
    eprint("thetagp: " + message)
    sys.exit(code)

# ── the arm table, read from the schemas ──

# ── the arm table, read from the generated descriptors ──

def _arms_of(message):
    """(arm name, arm message name, field number) of the envelope's oneof."""
    oneof = message.DESCRIPTOR.oneofs_by_name.get("kind")
    if oneof is None:
        fail("message %s carries no oneof kind" % message.DESCRIPTOR.name,
             EXIT_DEPENDENCY)
    return [(field.name, field.message_type.name, field.number)
            for field in sorted(oneof.fields, key=lambda f: f.number)]

class Arms:
    """The two envelopes' arms, as the generated descriptors declare them."""

    def __init__(self, binding):
        self.request = _arms_of(binding.Request)
        self.reply = _arms_of(binding.Reply)
        self.request_by_name = {name: (number, message)
                                for name, message, number in self.request}
        self.reply_by_name = {name: (number, message)
                              for name, message, number in self.reply}

        # An arm carries a command when the reply envelope has an arm of the same
        # name; the arms a body's write continues in are named after their own
        self.commands = {}
        self.continuations = {}
        for name, message, number in self.request:
            if name in self.reply_by_name:
                domain, _, rest = name.partition("_")
                self.commands["%s.%s" % (domain, rest)] = (number, message, name)
            else:
                self.continuations[name] = (number, message)
        self.command_by_arm = {name: command
                               for command, (_, _, name) in self.commands.items()}
        # The reply arms the request envelope has no arm of the same name for:
        # the two failure shapes and the two arms a body's read continues in.
        self.reply_only = {name: (number, message)
                           for name, message, number in self.reply
                           if name not in self.request_by_name}

    def arm_for_command(self, command):
        """The arm a name names.

        A command is <domain>.<name>, as in sys.ping. An arm a body's write
        continues in carries no command of its own -- the answer to it is the
        success arm of the request that opened the write -- so it is named by
        its own arm name, as in profile_put_end.
        """
        if command in self.continuations:
            number, message = self.continuations[command]
            return number, message, command
        if command.count(".") != 1:
            fail("%r is neither a command nor an arm: a command is "
                 "<domain>.<name>, as in sys.ping, and an arm a body's write "
                 "continues in is named by its own name, as in profile_put_end"
                 % command, EXIT_USAGE)
        if command not in self.commands:
            fail("no command %r in the protocol. It declares: %s"
                 % (command, ", ".join(sorted(self.commands))), EXIT_USAGE)
        return self.commands[command]

    def command_of_arm(self, arm_name):
        return self.command_by_arm.get(arm_name)

def payload_checksum(payload):
    """The sum of the payload's bytes in the low 16 bits (section 3.3)."""
    return sum(payload) & 0xFFFF

def encode_length(length):
    if length < 0 or length > FRAME_PAYLOAD_MAX:
        fail("a payload of %d bytes is above the %d byte maximum (ADR-0007 "
             "section 3.5)" % (length, FRAME_PAYLOAD_MAX), EXIT_USAGE)
    out = bytearray()
    value = length
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)

def encode_frame(payload):
    """One frame: varint L, the payload, then the checksum, little endian."""
    if len(payload) > FRAME_PAYLOAD_MAX:
        fail("a payload of %d bytes is above the %d byte maximum: no frame "
             "carries it (ADR-0007 section 3.5)"
             % (len(payload), FRAME_PAYLOAD_MAX), EXIT_USAGE)
    return (encode_length(len(payload)) + payload +
            payload_checksum(payload).to_bytes(2, "little"))

def hex_bytes(data):
    return " ".join("%02x" % byte for byte in data)

class FrameAssembler:
    """The frame layer over a byte stream, one byte at a time."""

    def __init__(self):
        # The counters are the run's and not the state machine's: resetting the
        # assembler to a length prefix (which every refusal and every complete
        self.frames = 0
        self.dropped = 0
        self.reset()

    def reset(self):
        self._state = "length"
        self._prefix = bytearray()
        self._payload = bytearray()
        self._length = 0
        self._checksum = bytearray()

    def feed(self, data):
        events = []
        for byte in data:
            events.extend(self._feed_byte(byte))
        return events

    def _feed_byte(self, byte):
        if self._state == "length":
            if len(self._prefix) == FRAME_PREFIX_MAX_BYTES:
                return self._drop("the length prefix is longer than %d bytes: "
                                  "a third prefix byte is refused where it "
                                  "arrives (ADR-0007 section 3.1)"
                                  % FRAME_PREFIX_MAX_BYTES)
            self._prefix.append(byte)
            if byte & 0x80:
                return []
            value = 0
            for index, part in enumerate(self._prefix):
                value |= (part & 0x7F) << (7 * index)
            if value > FRAME_PAYLOAD_MAX:
                return self._drop("the length prefix says %d bytes, above the "
                                  "%d byte maximum (ADR-0007 section 3.5)"
                                  % (value, FRAME_PAYLOAD_MAX))
            self._length = value
            self._payload = bytearray()
            self._checksum = bytearray()
            self._state = "payload" if value else "checksum"
            return []
        if self._state == "payload":
            self._payload.append(byte)
            if len(self._payload) == self._length:
                self._state = "checksum"
            return []
        self._checksum.append(byte)
        if len(self._checksum) < FRAME_CHECKSUM_BYTES:
            return []
        declared = int.from_bytes(bytes(self._checksum), "little")
        calculated = payload_checksum(self._payload)
        payload = bytes(self._payload)
        if declared != calculated:
            return self._drop("the checksum is 0x%04x and the payload sums to "
                              "0x%04x, so the frame is dropped whole and "
                              "nothing of it is decoded (ADR-0007 section 3.4)"
                              % (declared, calculated))
        self.reset()
        self.frames += 1
        return [("frame", payload)]

    def _drop(self, reason):
        self.reset()
        self.dropped += 1
        return [("drop", reason)]

    def half_frame_reason(self):
        """What is held mid-frame, or None when the assembler is at a prefix."""
        if self._state == "length":
            return None
        if self._state == "payload":
            missing = self._length - len(self._payload)
            return ("a half frame: the length prefix says %d bytes and %d of "
                    "them never arrived" % (self._length, missing))
        return ("a half frame: the payload of %d bytes is complete and %d of "
                "the %d checksum bytes arrived"
                % (self._length, len(self._checksum), FRAME_CHECKSUM_BYTES))

    def discard_half(self):
        """A half frame at the end of a read: dropped, and counted like one."""
        reason = self.half_frame_reason()
        if reason is None:
            return None
        self.dropped += 1
        self.reset()
        return reason

def read_hex(text):
    """A hex string as bytes, tolerant of the shapes a capture is written in."""
    cleaned = re.sub(r"0x", "", text, flags=re.I)
    cleaned = re.sub(r"[\s,;:_-]", "", cleaned)
    if not cleaned:
        fail("no bytes given", EXIT_USAGE)
    if len(cleaned) % 2 or re.search(r"[^0-9a-fA-F]", cleaned):
        fail("%r is not a whole number of hex bytes" % text.strip(),
             EXIT_USAGE)
    return bytes.fromhex(cleaned)

# ── the payload codec: the runtime and the generated bindings ──

class Bindings:
    def __init__(self, request_pb2, reply_pb2, text_format, json_format,
                 descriptor_module, message_factory):
        self.request = request_pb2
        self.reply = reply_pb2
        self.text_format = text_format
        self.json_format = json_format
        self.descriptor = descriptor_module
        self.message_factory = message_factory

    def envelope(self, kind):
        return self.request.Request if kind == "request" else self.reply.Reply

    def envelope_name(self, kind):
        return "Request" if kind == "request" else "Reply"

    def arm_class(self, field):
        """The class of the message an arm carries."""
        factory = getattr(self.message_factory, "GetMessageClass", None)
        if factory is not None:
            return factory(field.message_type)
        return field.message_type._concrete_class  # older runtimes

def pyserial_hint():
    return ("pyserial is not importable by %s, and this command reads or "
            "writes a port.\nInstall it outside the repository and run this "
            "tool with it:\n"
            "  python3 -m venv %s\n"
            "  %s -m pip install pyserial\n"
            "  %s scripts/tools/thetagp.py ...\n"
            "an interpreter whose site-packages already carries it (on Arch, "
            "the system python) works too.\n"
            "decode reads a hex string or a file and needs none of this."
            % (sys.executable, SUGGESTED_VENV,
               os.path.join(SUGGESTED_VENV, "bin", "python"),
               os.path.join(SUGGESTED_VENV, "bin", "python")))

def protobuf_hint(what):
    site_packages = sorted(glob.glob(os.path.join(SUGGESTED_VENV, "lib",
                                                  "python*", "site-packages")))
    return ("%s, and this command needs the payload codec.\n"
            "  interpreter: %s\n  %s: %s\n"
            "Install the runtime outside the repository and run this tool with "
            "it:\n  python3 -m venv %s\n  %s -m pip install protobuf\n"
            "  %s scripts/tools/thetagp.py ...\n"
            "or name an existing site-packages directory holding "
            "google/protobuf:\n  %s=%s scripts/tools/thetagp.py ..."
            % (what, sys.executable, PROTOBUF_PYTHONPATH_ENV,
               os.environ.get(PROTOBUF_PYTHONPATH_ENV) or "(not set)",
               SUGGESTED_VENV, os.path.join(SUGGESTED_VENV, "bin", "python"),
               os.path.join(SUGGESTED_VENV, "bin", "python"),
               PROTOBUF_PYTHONPATH_ENV,
               site_packages[-1] if site_packages
               else os.path.join(SUGGESTED_VENV, "lib", "pythonX.Y",
                                 "site-packages")))

def require_bindings(bindings_dir):
    """The runtime and the generated bindings, or a hard failure."""
    try:
        import google.protobuf  # noqa: F401  (the probe is the point)
        from google.protobuf import descriptor as descriptor_module
        from google.protobuf import json_format, message_factory, text_format
    except ImportError:
        fail(protobuf_hint("python-protobuf is not importable"),
             EXIT_DEPENDENCY)
    if bindings_dir not in sys.path:
        sys.path.insert(0, bindings_dir)
    try:
        import ThetaGP_pb2
    except ImportError as error:
        fail("the generated bindings in %s are not importable (%s). Generate "
             "them first:\n  python3 scripts/gen_proto_py.py"
             % (bindings_dir, error), EXIT_DEPENDENCY)
    return Bindings(ThetaGP_pb2, ThetaGP_pb2, text_format, json_format,
                    descriptor_module, message_factory)

def parse_payload(payload, kind, bindings):
    """One payload as the envelope it is, refusing a payload that is not one."""
    message = bindings.envelope(kind)()
    try:
        read = message.ParseFromString(payload)
    except Exception as error:  # DecodeError, and whatever the runtime raises
        fail("the payload of %d bytes is not a %s: %s (ADR-0007 section 3.1: a "
             "frame carries one whole message and nothing else)"
             % (len(payload), bindings.envelope_name(kind), error), EXIT_FRAME)
    if read is not None and read != len(payload):
        fail("the payload of %d bytes is a %s followed by %d bytes that are "
             "not part of it" % (len(payload), bindings.envelope_name(kind),
                                 len(payload) - read), EXIT_FRAME)
    return message

def render_frame(payload, kind, bindings, arms, frame_number, as_json):
    message = parse_payload(payload, kind, bindings)
    descriptor = message.DESCRIPTOR
    arm_name = message.WhichOneof("kind")
    arm_number = None
    arm_message = None
    command = None
    if arm_name is not None:
        field = descriptor.fields_by_name[arm_name]
        arm_number = field.number
        arm_message = field.message_type.name
        command = arms.command_of_arm(arm_name)
    queued = None
    if kind == "reply":
        queued = {"present": message.HasField("queued"),
                  "value": message.queued}

    if as_json:
        record = {
            "frame": frame_number,
            "kind": kind,
            "wire_bytes": len(payload) + len(encode_length(len(payload))) +
                          FRAME_CHECKSUM_BYTES,
            "payload_bytes": len(payload),
            "payload_hex": payload.hex(),
            "length": len(payload),
            "checksum": "0x%04x" % payload_checksum(payload),
            "arm": {"name": arm_name, "number": arm_number,
                    "message": arm_message, "command": command},
            "queued": queued,
            "fields": bindings.json_format.MessageToDict(
                message, preserving_proto_field_name=True),
        }
        print(json.dumps(record, sort_keys=True))
        return message

    prefix = "frame %d" % frame_number
    print("%s  %d B on the wire, L=%d, checksum 0x%04x (the payload's own sum) "
          "ok" % (prefix, len(payload) + len(encode_length(len(payload))) +
                  FRAME_CHECKSUM_BYTES, len(payload), payload_checksum(payload)))
    print("  kind    %s" % kind)
    if arm_name is None:
        print("  arm     none set (which_kind == 0: the envelope carries no "
              "arm, ADR-0007 section 4.4a rule 3 -> the device answers "
              "Error(ERR_UNKNOWN_CMD))")
    else:
        print("  arm     %s (%s field %d, %s)" % (arm_name,
                                                  bindings.envelope_name(kind),
                                                  arm_number, arm_message))
        if command:
            print("  command %s" % command)
    if queued is not None:
        print("  queued  %s (%s field 34)"
              % (queued["value"] if queued["present"]
                 else "absent -- the request's queued was not read, section "
                      "10.1 ruling 5",
                 bindings.envelope_name(kind)))
    print("  payload %s" % hex_bytes(payload))
    body = bindings.text_format.MessageToString(message, as_one_line=False)
    for line in body.rstrip("\n").splitlines():
        print("    " + line)
    return message

# ── inputs, ports and the frame layer over them ──

def decode_events(events, half, kind, bindings, arms, as_json):
    """Decode the events a buffer produced; returns True when all was read."""
    ok = True
    index = 0
    for event in events:
        if event[0] == "drop":
            eprint("thetagp: %s" % event[1])
            ok = False
            continue
        index += 1
        render_frame(event[1], kind, bindings, arms, index, as_json)
    if half is not None:
        eprint("thetagp: %s -- dropped (ADR-0007 section 3.4 rule 5, "
               "--idle-timeout)" % half)
        ok = False
    return ok

def cmd_decode(args):
    bindings = require_bindings(args.bindings)
    arms = Arms(bindings.request)
    if args.file:
        try:
            with open(args.file, "rb") as handle:
                data = handle.read()
        except OSError as error:
            fail("cannot read %s: %s" % (args.file, error), EXIT_USAGE)
    else:
        data = read_hex(" ".join(args.bytes))
    if not data:
        fail("no bytes to decode", EXIT_USAGE)

    assembler = FrameAssembler()
    events = assembler.feed(data)
    half = assembler.half_frame_reason()
    if not events and half is None:
        fail("no frame in %d bytes" % len(data), EXIT_FRAME)

    if not any(event[0] == "frame" for event in events):
        decode_events(events, half, args.kind, None, arms, args.json)
        return EXIT_FRAME

    ok = decode_events(events, half, args.kind, bindings, arms, args.json)
    if len(events) > 1 or half is not None:
        eprint("thetagp: %d frame(s) decoded, the frame layer dropped %d"
               % (assembler.frames, assembler.dropped))
    return EXIT_OK if ok else EXIT_FRAME

def serial_module():
    try:
        import serial  # noqa: F401  (the probe is the point)
    except ImportError:
        fail(pyserial_hint(), EXIT_DEPENDENCY)
    return serial

def open_port(args):
    serial = serial_module()
    try:
        return serial.Serial(args.port, args.baud, timeout=0.05)
    except Exception as error:
        fail("cannot open %s: %s" % (args.port, error), EXIT_IO)

def watch_port(args, kind, on_frame, deadline=None, count=None):
    """Read the port into the frame layer until the deadline or the count."""
    assembler = FrameAssembler()
    port = open_port(args)
    seen = 0
    last_byte = time.monotonic()
    try:
        while True:
            if deadline is not None and time.monotonic() >= deadline:
                break
            if count is not None and seen >= count:
                break
            chunk = port.read(max(1, port.in_waiting))
            now = time.monotonic()
            if chunk:
                last_byte = now
                for event in assembler.feed(chunk):
                    if event[0] == "drop":
                        eprint("thetagp: %s" % event[1])
                    else:
                        seen += 1
                        on_frame(event[1], seen)
            else:
                half = assembler.half_frame_reason()
                if half is not None and now - last_byte > args.idle_timeout:
                    assembler.discard_half()
                    eprint("thetagp: %s -- dropped after %.1f s of silence "
                           "(ADR-0007 section 3.4 rule 5)"
                           % (half, args.idle_timeout))
    finally:
        half = assembler.discard_half()
        if half is not None:
            eprint("thetagp: %s -- dropped" % half)
        port.close()
    return seen, assembler

def cmd_watch(args):
    bindings = require_bindings(args.bindings)
    arms = Arms(bindings.request)

    def on_frame(payload, index):
        render_frame(payload, args.kind, bindings, arms, index, args.json)

    deadline = time.monotonic() + args.seconds if args.seconds else None
    try:
        seen, assembler = watch_port(args, args.kind, on_frame, deadline,
                                     args.count)
    except KeyboardInterrupt:
        print("")
        eprint("thetagp: interrupted")
        return EXIT_OK
    eprint("thetagp: %d frame(s) decoded, the frame layer dropped %d"
           % (seen, assembler.dropped))
    return EXIT_FRAME if assembler.dropped else EXIT_OK

# ── building a request from a command name ──

INT_TYPES = None  # filled from the runtime, which is where the numbers come from

def field_types(bindings):
    global INT_TYPES
    if INT_TYPES is None:
        descriptor = bindings.descriptor.FieldDescriptor
        INT_TYPES = {descriptor.TYPE_INT32, descriptor.TYPE_INT64,
                     descriptor.TYPE_UINT32, descriptor.TYPE_UINT64,
                     descriptor.TYPE_SINT32, descriptor.TYPE_SINT64,
                     descriptor.TYPE_FIXED32, descriptor.TYPE_FIXED64,
                     descriptor.TYPE_SFIXED32, descriptor.TYPE_SFIXED64}
    return INT_TYPES

def is_repeated(field):
    repeated = getattr(field, "is_repeated", None)
    if repeated is not None:
        return repeated
    return field.label == field.LABEL_REPEATED

def coerce(field, raw, bindings):
    types = field_types(bindings)
    if field.type in types:
        try:
            return int(raw, 0)
        except ValueError:
            fail("%s takes a number, not %r" % (field.name, raw), EXIT_USAGE)
    if field.type == field.TYPE_BOOL:
        lowered = raw.strip().lower()
        if lowered in ("1", "true", "yes", "on"):
            return True
        if lowered in ("0", "false", "no", "off"):
            return False
        fail("%s takes a boolean, not %r" % (field.name, raw), EXIT_USAGE)
    if field.type in (field.TYPE_FLOAT, field.TYPE_DOUBLE):
        try:
            return float(raw)
        except ValueError:
            fail("%s takes a number, not %r" % (field.name, raw), EXIT_USAGE)
    if field.type == field.TYPE_STRING:
        return raw
    if field.type == field.TYPE_BYTES:
        return read_hex(raw)
    if field.type == field.TYPE_ENUM:
        values = field.enum_type.values_by_name
        if raw in values:
            return values[raw].number
        try:
            return int(raw, 0)
        except ValueError:
            fail("%s takes one of %s, not %r"
                 % (field.name, ", ".join(sorted(values)), raw), EXIT_USAGE)
    fail("%s is a message field: name one of its own fields with a dotted "
         "path, as in array.elements=1,2" % field.name, EXIT_USAGE)

def set_field(message, path, raw, bindings):
    parts = path.split(".")
    for part in parts[:-1]:
        field = message.DESCRIPTOR.fields_by_name.get(part)
        if field is None:
            fail("%s carries no field %r; it carries: %s"
                 % (message.DESCRIPTOR.name, part,
                    ", ".join(sorted(message.DESCRIPTOR.fields_by_name))),
                 EXIT_USAGE)
        if field.type != field.TYPE_MESSAGE:
            fail("%s is not a message field, so %r names nothing inside it"
                 % (part, path), EXIT_USAGE)
        message = getattr(message, part)
    leaf = parts[-1]
    field = message.DESCRIPTOR.fields_by_name.get(leaf)
    if field is None:
        fail("%s carries no field %r; it carries: %s"
             % (message.DESCRIPTOR.name, leaf,
                ", ".join(sorted(message.DESCRIPTOR.fields_by_name))),
             EXIT_USAGE)
    if is_repeated(field):
        if field.type == field.TYPE_MESSAGE:
            fail("%s is a repeated message field: this tool does not build "
                 "one from the command line" % leaf, EXIT_USAGE)
        getattr(message, leaf).extend(coerce(field, part, bindings)
                                      for part in raw.split(","))
        return
    if field.type == field.TYPE_MESSAGE:
        fail("%s is a message field: name one of its own fields with a dotted "
             "path, as in array.elements=1,2" % leaf, EXIT_USAGE)
    setattr(message, leaf, coerce(field, raw, bindings))

class Sequence:
    """The frame numbers the host writes. Every frame the host puts on the wire
    takes the next number, a reply carries the number of its request plus one,
    and the host's next frame carries that reply's number plus one."""

    def __init__(self):
        self.next = 0

    def take(self):
        number = self.next
        self.next = number + 1
        return number

    def after_reply(self, number):
        self.next = number + 1


_sequence = Sequence()

def build_request(command, assignments, bindings, arms):
    arm_number, arm_message, arm_name = arms.arm_for_command(command)
    envelope = bindings.request.Request()
    field = envelope.DESCRIPTOR.fields_by_name[arm_name]
    arm = bindings.arm_class(field)()
    for assignment in assignments:
        if "=" not in assignment:
            fail("%r is not field=value" % assignment, EXIT_USAGE)
        path, _, raw = assignment.partition("=")
        set_field(arm, path, raw, bindings)
    envelope.queued = _sequence.take()
    getattr(envelope, arm_name).CopyFrom(arm)
    payload = envelope.SerializeToString()
    if not payload:
        fail("the request encodes to no bytes at all, which is not a frame",
             EXIT_USAGE)
    return payload, arm_name, arm_number, arm_message, envelope.queued

def report_frame(payload, arm_name, arm_number, arm_message, command=None,
                 number=None):
    print("arm     %d (%s, %s)" % (arm_number, arm_name, arm_message))
    if command:
        print("command %s" % command)
    if number is not None:
        print("queued  %d (the reply carries %d)" % (number, number + 1))
    print("payload %d B: %s" % (len(payload), hex_bytes(payload)))
    frame = encode_frame(payload)
    print("frame   %d B: %s" % (len(frame), hex_bytes(frame)))
    return frame

def cmd_send(args):
    bindings = require_bindings(args.bindings)
    arms = Arms(bindings.request)
    payload, arm_name, arm_number, arm_message, number = build_request(
        args.command, args.fields, bindings, arms)
    frame = report_frame(payload, arm_name, arm_number, arm_message,
                         arms.command_of_arm(arm_name), number)
    if args.dry_run:
        print("dry run: nothing was written to a port")
        return EXIT_OK
    port = open_port(args)
    try:
        written = port.write(frame)
        port.flush()
    finally:
        port.close()
    if written != len(frame):
        fail("the port took %d of the frame's %d bytes" % (written, len(frame)),
             EXIT_IO)
    print("%s: wrote %d bytes" % (args.port, written))
    return EXIT_OK

def cmd_ping(args):
    bindings = require_bindings(args.bindings)
    arms = Arms(bindings.request)
    port = open_port(args)
    try:
        for attempt in range(1, PING_ATTEMPTS + 1):
            payload, arm_name, arm_number, arm_message, number = build_request(
                "sys.ping", [], bindings, arms)
            frame = report_frame(payload, arm_name, arm_number, arm_message,
                                 "sys.ping", number)
            port.reset_input_buffer()
            port.write(frame)
            port.flush()
            eprint("thetagp: sys.ping sent (attempt %d of %d), waiting %.1f s"
                   % (attempt, PING_ATTEMPTS, args.timeout))
            assembler = FrameAssembler()
            deadline = time.monotonic() + args.timeout
            answered = []
            transport_error = False
            while time.monotonic() < deadline and not answered:
                chunk = port.read(max(1, port.in_waiting))
                if not chunk:
                    continue
                for event in assembler.feed(chunk):
                    if event[0] == "drop":
                        eprint("thetagp: %s" % event[1])
                        continue
                    message = render_frame(event[1], "reply", bindings, arms,
                                           len(answered) + 1, args.json)
                    answered.append(message)
                    if message.WhichOneof("kind") == "transport_error":
                        transport_error = True
                    elif message.queued != number + 1:
                        eprint("thetagp: the reply is numbered %d, and the "
                               "frame was %d" % (message.queued, number))
                    else:
                        _sequence.after_reply(message.queued)
            if answered and not transport_error:
                return EXIT_OK
            if transport_error:
                eprint("thetagp: TransportError -- flushing the port and "
                       "sending sys.ping again")
    finally:
        port.close()
    fail("no answer to sys.ping in %d attempts of %.1f s"
         % (PING_ATTEMPTS, args.timeout), EXIT_IO)

def cmd_commands(args):
    """Print the command-to-arm mapping."""
    bindings = require_bindings(args.bindings)
    arms = Arms(bindings.request)
    print("%d commands, %d request arms, %d reply arms, read from the "
          "generated descriptors"
          % (len(arms.commands), len(arms.request), len(arms.reply)))
    for command in sorted(arms.commands,
                          key=lambda name: arms.commands[name][0]):
        number, message, arm_name = arms.commands[command]
        reply_number, reply_message = arms.reply_by_name[arm_name]
        print("  %-24s request arm %2d  %-18s reply arm %2d  %s"
              % (command, number, message, reply_number, reply_message))
    for arm_name, (number, message) in sorted(arms.continuations.items(),
                                              key=lambda item: item[1][0]):
        print("  %-24s request arm %2d  %-18s no command of its own: a body's "
              "write continues in it, and this arm name is what sends it "
              "(section 4.4a rule 6)" % (arm_name, number, message))
    for arm_name, (number, message) in sorted(arms.reply_only.items(),
                                              key=lambda item: item[1][0]):
        print("  %-24s reply arm %2d  %-18s no request arm of that name: the "
              "failure shapes (section 4.3) and a body's read (section 3.6)"
              % ("(no command)", number, message))
    print("request arms 1..%d and reply arms 1..%d, both dense from 1 "
          "(section 4.4a rule 1)" % (len(arms.request), len(arms.reply)))
    return EXIT_OK

# ── the command line ──

def common_parser():
    """The flags every subcommand takes, so they may follow the subcommand."""
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--bindings", default=argparse.SUPPRESS, metavar="DIR",
                        help="the generated bindings")
    parser.add_argument("--json", action="store_true", default=argparse.SUPPRESS,
                        help="one JSON object per decoded frame (JSON lines) "
                             "instead of the readable dump")
    return parser

def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="thetagp.py",
        description="Read, write and watch the ThetaGP CDC protocol: frames "
                    "as ADR-0007 section 3.1 defines them, payloads as the "
                    "schemas of the protocol define them.")
    common = common_parser()
    parser.add_argument("--bindings", default=DEFAULT_BINDINGS_DIR, metavar="DIR",
                        help="the generated bindings (default: the newest "
                             "build*/proto_py/, which is where the configure "
                             "wrote them)")
    parser.add_argument("--json", action="store_true",
                        help="one JSON object per decoded frame (JSON lines) "
                             "instead of the readable dump")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    decode = subparsers.add_parser(
        "decode", parents=[common],
        help="a hex string or a file of frames -> text")
    decode.add_argument("bytes", nargs="*", metavar="HEX",
                        help="the frame as hex, as in '05 1a 00 90 02 07 b3 00'")
    decode.add_argument("--file", metavar="PATH",
                        help="read the bytes from a file instead (raw bytes)")
    decode.add_argument("--kind", choices=("reply", "request"), default="reply",
                        help="which envelope the payload is (default: reply)")
    decode.set_defaults(handler=cmd_decode)

    watch = subparsers.add_parser(
        "watch", parents=[common],
        help="read a port and decode every frame that arrives")
    watch.add_argument("--port", required=True, help="the serial port")
    watch.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                       help="kept for the port call; CDC ACM carries its own "
                            "rate (default: %(default)s)")
    watch.add_argument("--kind", choices=("reply", "request"), default="reply",
                       help="which envelope the payloads are (default: reply)")
    watch.add_argument("--idle-timeout", type=float,
                       default=DEFAULT_IDLE_TIMEOUT_S, metavar="SECONDS",
                       help="drop a half frame after this much silence "
                            "(default: %(default)s)")
    watch.add_argument("--seconds", type=float, default=None,
                       help="stop after this many seconds (default: run until "
                            "interrupted)")
    watch.add_argument("--count", type=int, default=None,
                       help="stop after this many frames")
    watch.set_defaults(handler=cmd_watch)

    send = subparsers.add_parser(
        "send", parents=[common],
        help="build one frame from a command, or from an arm a body's write "
             "continues in, and write it out")
    send.add_argument("command",
                      help="the command, as in sys.ping, or the name of an arm "
                           "a body's write continues in, as in profile_put_end")
    send.add_argument("fields", nargs="*", metavar="field=value",
                      help="the arm's fields; numbers take 0x too, bytes take "
                           "hex, repeated scalars take commas, a nested "
                           "message takes a dotted path (array.elements=1,2)")
    send.add_argument("--port", help="the serial port; not needed with --dry-run")
    send.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                      help="kept for the port call (default: %(default)s)")
    send.add_argument("--dry-run", action="store_true",
                      help="print the frame and touch no port")
    send.set_defaults(handler=cmd_send)

    ping = subparsers.add_parser(
        "ping", parents=[common],
        help="sys.ping: send it and wait for the answer")
    ping.add_argument("--port", required=True, help="the serial port")
    ping.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                      help="kept for the port call (default: %(default)s)")
    ping.add_argument("--timeout", type=float, default=PING_TIMEOUT_S,
                      metavar="SECONDS",
                      help="how long to wait for the answer (default: "
                           "%(default)s)")
    ping.set_defaults(handler=cmd_ping)

    commands = subparsers.add_parser(
        "commands", parents=[common],
        help="print the command -> arm mapping the schemas carry")
    commands.set_defaults(handler=cmd_commands)

    args = parser.parse_args(argv)
    return args

def honor_protobuf_pythonpath():
    """$PROTOBUF_PYTHONPATH names a site-packages directory, as it does for the"""
    for entry in os.environ.get(PROTOBUF_PYTHONPATH_ENV, "").split(os.pathsep):
        if entry and entry not in sys.path:
            sys.path.insert(0, entry)

def main(argv):
    honor_protobuf_pythonpath()
    args = parse_args(argv)
    if args.command_name == "send" and args.port is None and not args.dry_run:
        fail("send needs --port, or --dry-run to build the frame and write "
             "nothing", EXIT_USAGE)
    return args.handler(args)

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
