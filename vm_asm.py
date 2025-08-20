#!/usr/bin/env python3
"""
VM Assembler — Module 1
-----------------------
Goal: Parse a simple VM assembly file (.vmasm) and emit a compact bytecode file (.vmc)
with a minimal header, constant pool (optional, future), and code section.

File formats (v0.1)
===================
Human-readable assembly (.vmasm)
--------------------------------
- UTF‑8 text, one logical instruction per line.
- Structure per line: [label:] <mnemonic> [operands] [; comment]
- Directives start with a dot: .text (default), .data (reserved), .global <label>
- Labels end with ':' and may be referenced by branch/call operands.
- Registers: r0..r31 (aliases x0..x31). Immediate ints: decimal (42), hex (0x2A), binary (0b101010).
- Whitespace is flexible; commas separate operands.

Example:
    ; add two numbers
    .text
    .global main
main:
    ldi r1, 5          ; r1 = 5
    ldi r2, 7          ; r2 = 7
    add r3, r1, r2     ; r3 = 12
    halt

Bytecode container (.vmc)
-------------------------
All values little‑endian.
Header (fixed 16 bytes):
  0..3   magic     = 0x564D4346  (ASCII 'VMCF')
  4..5   version   = 0x0001      (v0.1)
  6..7   flags     = 0           (reserved)
  8..11  code_off  = offset (bytes) to code section from file start
  12..15 code_len  = length (bytes) of code section

Code section: linear sequence of encoded instructions.
Instruction encoding v0.1 (fixed width 8 bytes for simplicity in M1):
  Byte 0: opcode (u8)
  Byte 1: rd (u8)   — destination register index (0..31) or 0 if unused
  Byte 2: rs1 (u8)  — source register 1
  Byte 3: rs2 (u8)  — source register 2
  Byte 4..7: imm32 (i32) — immediate or displacement (used when applicable); otherwise 0

Rationale: fixed 8‑byte instructions keep Module 1 simple; later modules can
compact to variable encodings.

Supported mnemonics (initial subset):
  ldi  rd, imm              ; rd = imm
  mov  rd, rs1              ; rd = rs1
  add  rd, rs1, rs2         ; rd = rs1 + rs2
  sub  rd, rs1, rs2
  and  rd, rs1, rs2
  or   rd, rs1, rs2
  xor  rd, rs1, rs2
  lw   rd, [rs1+imm]        ; load word
  sw   rs2, [rs1+imm]       ; store word
  jmp  label                ; PC = label
  beq  rs1, rs2, label      ; if rs1==rs2 branch to label
  bne  rs1, rs2, label
  call label                ; push/pc handled by VM (future); here just branch
  ret                       ; return (opcode only)
  halt                      ; stop execution

Relocations / labels:
- One pass to collect labels (symbol table → byte offset in code).
- Second pass to encode branches/calls with resolved relative displacement (imm32 = target_offset - current_offset).

This module contains:
- Lexer/Tokeniser: converts source into tokens (identifiers, numbers, registers, punctuation, directives, labels, EOL).
- Parser: builds a flat list of instructions + operands (no expressions beyond simple numbers/registers).
- Assembler: resolves labels, encodes to bytecode, writes .vmc with header.
- CLI usage:
      python vm_asm.py assemble input.vmasm -o output.vmc
      python vm_asm.py dump output.vmc   # hex dump for inspection

The design keeps room to map this VM ISA to RISC‑V in a later backend.
"""
from __future__ import annotations
import argparse
import enum
import io
import os
import re
import struct
from dataclasses import dataclass
from typing import List, Tuple, Dict, Optional

# -----------------------------
# Tokeniser
# -----------------------------

TOKEN_SPEC = [
    ("WS",       r"[ \t]+"),
    ("COMMENT",  r";.*"),
    ("DIRECTIVE",r"\.[A-Za-z_][A-Za-z0-9_]*"),
    ("LABEL",    r"[A-Za-z_][A-Za-z0-9_]*:"),
    ("REGISTER", r"(?:r|x)(?:[12]?\d|3[01]|\d)\b"),
    ("HEX",      r"0x[0-9A-Fa-f]+"),
    ("BIN",      r"0b[01]+"),
    ("INT",      r"-?\d+"),
    ("IDENT",    r"[A-Za-z_][A-Za-z0-9_]*"),
    ("COMMA",    r","),
    ("LBRACK",   r"\["),
    ("RBRACK",   r"\]"),
    ("PLUS",     r"\+"),
    ("NEWLINE",  r"\n+"),
]
TOKEN_RE = re.compile("|".join(f"(?P<{name}>{pat})" for name, pat in TOKEN_SPEC))

@dataclass
class Token:
    kind: str
    value: str
    line: int
    col: int


def lex(src: str) -> List[Token]:
    tokens: List[Token] = []
    line = 1
    col = 1
    i = 0
    while i < len(src):
        m = TOKEN_RE.match(src, i)
        if not m:
            snippet = src[i:i+20].replace("\n", "\\n")
            raise SyntaxError(f"Unknown token at {line}:{col}: '{snippet}...' ")
        kind = m.lastgroup
        text = m.group(kind)
        if kind == "NEWLINE":
            tokens.append(Token(kind, "\n", line, col))
            line += text.count("\n")
            col = 1
        elif kind in ("WS", "COMMENT"):
            pass  # ignore
        else:
            tokens.append(Token(kind, text, line, col))
            col += len(text)
        i = m.end()
    tokens.append(Token("EOF", "", line, col))
    return tokens

# -----------------------------
# AST / IR
# -----------------------------

class Op(enum.Enum):
    LDI=1; MOV=2; ADD=3; SUB=4; AND=5; OR=6; XOR=7; LW=8; SW=9; JMP=10; BEQ=11; BNE=12; CALL=13; RET=14; HALT=15

MNEMONIC_TO_OP = {
    "ldi": Op.LDI,
    "mov": Op.MOV,
    "add": Op.ADD,
    "sub": Op.SUB,
    "and": Op.AND,
    "or":  Op.OR,
    "xor": Op.XOR,
    "lw":  Op.LW,
    "sw":  Op.SW,
    "jmp": Op.JMP,
    "beq": Op.BEQ,
    "bne": Op.BNE,
    "call":Op.CALL,
    "ret": Op.RET,
    "halt":Op.HALT,
}

@dataclass
class Instr:
    op: Op
    rd: int = 0
    rs1: int = 0
    rs2: int = 0
    imm: int = 0
    src_line: int = 0

# -----------------------------
# Parser (line‑oriented)
# -----------------------------

class Parser:
    def __init__(self, tokens: List[Token]):
        self.toks = tokens
        self.i = 0
        self.labels: Dict[str,int] = {}
        self.pending_fixups: List[Tuple[int,str,Token]] = []  # (instr_index, label, token)
        self.instrs: List[Instr] = []

    def at(self, kind: str) -> bool:
        return self.toks[self.i].kind == kind

    def eat(self, kind: str) -> Token:
        t = self.toks[self.i]
        if t.kind != kind:
            raise SyntaxError(f"Expected {kind} at {t.line}:{t.col}, got {t.kind}")
        self.i += 1
        return t

    def maybe(self, kind: str) -> Optional[Token]:
        if self.at(kind):
            return self.eat(kind)
        return None

    def parse(self) -> Tuple[List[Instr], Dict[str,int]]:
        while not self.at("EOF"):
            if self.at("NEWLINE"):
                self.eat("NEWLINE");
                continue
            # label
            if self.at("LABEL"):
                t = self.eat("LABEL")
                name = t.value[:-1]
                self.labels[name] = len(self.instrs) * 8  # each encodes to 8 bytes
                self.maybe("NEWLINE")
                continue
            # directive (minimal handling)
            if self.at("DIRECTIVE"):
                d = self.eat("DIRECTIVE").value.lower()
                # For Module 1 we accept .text/.data/.global and ignore others
                if d not in (".text", ".data", ".global"):
                    pass
                # optional IDENT after .global
                if d == ".global" and self.at("IDENT"):
                    self.eat("IDENT")
                # consume to EOL
                while not self.at("NEWLINE") and not self.at("EOF"):
                    self.i += 1
                self.maybe("NEWLINE")
                continue
            # instruction line
            if self.at("IDENT"):
                self.parse_instr()
                self.maybe("NEWLINE")
                continue
            # skip stray commas etc until newline to avoid infinite loops
            self.i += 1
        return self.instrs, self.labels

    def parse_reg(self) -> int:
        t = self.eat("REGISTER")
        n = int(t.value[1:])
        if not (0 <= n <= 31):
            raise SyntaxError(f"Register out of range at {t.line}:{t.col}")
        return n

    def parse_int(self) -> int:
        t = self.toks[self.i]
        if t.kind == "HEX":
            self.i += 1
            return int(t.value, 16)
        if t.kind == "BIN":
            self.i += 1
            return int(t.value, 2)
        if t.kind == "INT":
            self.i += 1
            return int(t.value, 10)
        raise SyntaxError(f"Expected integer at {t.line}:{t.col}")

    def expect_comma(self):
        if not self.maybe("COMMA"):
            t = self.toks[self.i]
            raise SyntaxError(f"Expected ',' at {t.line}:{t.col}")

    def parse_mem(self) -> Tuple[int,int]:
        # [rs1+imm]
        self.eat("LBRACK")
        base = self.parse_reg()
        off = 0
        if self.maybe("PLUS"):
            off = self.parse_int()
        self.eat("RBRACK")
        return base, off

    def parse_label_ref(self) -> Tuple[int, Optional[str], Token]:
        t = self.toks[self.i]
        if t.kind in ("IDENT",):
            self.i += 1
            return 0, t.value, t
        # allow numeric absolute for now
        imm = self.parse_int()
        return imm, None, t

    def parse_instr(self):
        mnem = self.eat("IDENT").value.lower()
        if mnem not in MNEMONIC_TO_OP:
            t = self.toks[self.i-1]
            raise SyntaxError(f"Unknown mnemonic '{mnem}' at {t.line}:{t.col}")
        op = MNEMONIC_TO_OP[mnem]
        line = self.toks[self.i-1].line
        rd=rs1=rs2=0; imm=0
        if op == Op.LDI:
            rd = self.parse_reg(); self.expect_comma(); imm = self.parse_int()
        elif op == Op.MOV:
            rd = self.parse_reg(); self.expect_comma(); rs1 = self.parse_reg()
        elif op in (Op.ADD, Op.SUB, Op.AND, Op.OR, Op.XOR):
            rd = self.parse_reg(); self.expect_comma(); rs1 = self.parse_reg(); self.expect_comma(); rs2 = self.parse_reg()
        elif op == Op.LW:
            rd = self.parse_reg(); self.expect_comma(); rs1, imm = self.parse_mem()
        elif op == Op.SW:
            rs2 = self.parse_reg(); self.expect_comma(); rs1, imm = self.parse_mem()
        elif op in (Op.JMP, Op.CALL):
            imm, label, tok = self.parse_label_ref()
            if label is not None:
                self.pending_fixups.append((len(self.instrs), label, tok))
        elif op in (Op.BEQ, Op.BNE):
            rs1 = self.parse_reg(); self.expect_comma(); rs2 = self.parse_reg(); self.expect_comma(); imm, label, tok = self.parse_label_ref()
            if label is not None:
                self.pending_fixups.append((len(self.instrs), label, tok))
        elif op in (Op.RET, Op.HALT):
            pass
        else:
            raise SyntaxError(f"Unhandled mnemonic {mnem}")
        self.instrs.append(Instr(op, rd, rs1, rs2, imm, line))

# -----------------------------
# Encoder / Assembler
# -----------------------------

MAGIC = 0x564D4346  # 'VMCF'
VERSION = 0x0001


def encode(instrs: List[Instr], labels: Dict[str,int]) -> bytes:
    code = bytearray()
    # resolve pending label fixups by computing relative displacement in bytes
    # displacement = target_offset - current_offset
    for idx, inst in enumerate(instrs):
        code += struct.pack("<BBBBi", inst.op.value, inst.rd, inst.rs1, inst.rs2, 0)
    # second pass: apply fixups
    for i, label, tok in Parser([]).pending_fixups:  # dummy; real fixups are stored in parser instance, but we can't access here
        pass
    # We'll instead perform fixups inline below by re-encoding with resolved imm
    code = bytearray()
    for idx, inst in enumerate(instrs):
        imm = inst.imm
        if inst.op in (Op.JMP, Op.CALL, Op.BEQ, Op.BNE) and isinstance(imm, int) and imm == 0:
            # try to see if the instruction carried a label via a placeholder in imm (0). In Module 1
            # we encoded label references as imm=0 and stored a synthetic attribute '_label'
            pass
        code += struct.pack("<BBBBi", inst.op.value, inst.rd, inst.rs1, inst.rs2, imm)
    return bytes(code)


def assemble(src: str) -> bytes:
    tokens = lex(src)
    parser = Parser(tokens)
    instrs, labels = parser.parse()
    # resolve label fixups
    for idx, label, tok in parser.pending_fixups:
        if label not in labels:
            raise SyntaxError(f"Undefined label '{label}' at {tok.line}:{tok.col}")
        target = labels[label]
        here = idx * 8
        disp = target - here
        instrs[idx].imm = disp
    code = encode(instrs, labels)
    # header
    code_off = 16
    header = struct.pack("<IHHII", MAGIC, VERSION, 0, code_off, len(code))
    return header + code

# -----------------------------
# Utilities
# -----------------------------

def dump(b: bytes) -> str:
    out = io.StringIO()
    for i in range(0, len(b), 16):
        chunk = b[i:i+16]
        hexs = " ".join(f"{x:02X}" for x in chunk)
        out.write(f"{i:08X}  {hexs}\n")
    return out.getvalue()

# -----------------------------
# CLI
# -----------------------------

def main():
    ap = argparse.ArgumentParser(description="VM Assembler (Module 1)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    ap_a = sub.add_parser("assemble", help="Assemble .vmasm → .vmc")
    ap_a.add_argument("input", help="input .vmasm file")
    ap_a.add_argument("-o", "--output", default=None, help="output .vmc file")

    ap_d = sub.add_parser("dump", help="Hex‑dump a .vmc file")
    ap_d.add_argument("file", help=".vmc file")

    args = ap.parse_args()
    if args.cmd == "assemble":
        with open(args.input, "r", encoding="utf-8") as f:
            src = f.read()
        blob = assemble(src)
        out = args.output or os.path.splitext(args.input)[0] + ".vmc"
        with open(out, "wb") as f:
            f.write(blob)
        print(f"Wrote {out} ({len(blob)} bytes)")
    elif args.cmd == "dump":
        with open(args.file, "rb") as f:
            b = f.read()
        print(dump(b))

if __name__ == "__main__":
    main()
