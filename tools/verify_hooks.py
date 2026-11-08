"""Verify AnimationMotionFix call and vtable hooks against Skyrim 1.5.97/1.6.1170.

Requires Python 3.10+ and capstone (pip install capstone). Reads the project source,
CommonLib headers, Address Libraries, and unpacked executables. It never launches
or modifies Skyrim.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_64, Cs


ROOTS = Path(__file__).resolve().parents[2]
EXPECTED_CALLS = [
    {"name": "PitchTranslation", "se_id": 36365, "ae_id": 37356, "se_offset": 0x365, "ae_offset": 0x3FE, "se_target": 0x10FA2F0, "ae_target": 0x11F19A0},
    {"name": "PlayerRotation", "se_id": 39379, "ae_id": 40451, "se_offset": 0x42, "ae_offset": 0x42, "se_target": 0x6CF2A0, "ae_target": 0x7635E0},
    {"name": "Movement", "se_id": 36357, "ae_id": 37348, "se_offset": 0x222, "ae_offset": 0x1FB, "se_target": 0x6274A0, "ae_target": 0x6B8EA0},
    {"name": "ProxyProxy", "se_id": 77248, "ae_id": 79134, "se_offset": 0x3F, "ae_offset": 0x37, "se_target": 0xDC1C60, "ae_target": 0xEA13A0},
    {"name": "ProxyRigid", "se_id": 77244, "ae_id": 79130, "se_offset": 0x3CD, "ae_offset": 0x3B1, "se_target": 0xDC1C60, "ae_target": 0xEA13A0},
    {"name": "RigidProxy", "se_id": 77321, "ae_id": 79201, "se_offset": 0x159, "ae_offset": 0x147, "se_target": 0xDF35B0, "ae_target": 0xED5360},
    {"name": "RigidRigid", "se_id": 77306, "ae_id": 79186, "se_offset": 0x29F, "ae_offset": 0x2A9, "se_target": 0xDC1C60, "ae_target": 0xEA13A0},
]
EXPECTED_VTABLES = [
    {
        "name": "ProcessConstraintsCallback",
        "class": "bhkCharProxyController",
        "slot": 1,
        "se_target": 0xDEF090,
        "ae_target": 0xED0B40,
        "declaration": [
            ("include/RE/B/bhkCharProxyController.h", "public hkpCharacterProxyListener"),
            ("include/RE/B/bhkCharProxyController.h", "ProcessConstraintsCallback(const hkpCharacterProxy*"),
            ("include/RE/H/hkpCharacterProxyListener.h", "ProcessConstraintsCallback(const hkpCharacterProxy*"),
        ],
        "claim": "bhkCharProxyController's primary base is hkpCharacterProxyListener; ProcessConstraintsCallback is inherited slot 1.",
    },
    {
        "name": "DeleteThis",
        "class": "bhkCharRigidBodyController",
        "slot": 1,
        "se_target": 0xC61A30,
        "ae_target": 0xD27520,
        "declaration": [
            ("include/RE/B/bhkCharRigidBodyController.h", "public bhkCharacterController"),
            ("include/RE/B/bhkCharacterController.h", "public NiRefObject"),
            ("include/RE/N/NiRefObject.h", "virtual void DeleteThis();  // 01"),
        ],
        "claim": "The primary controller base inherits NiRefObject; DeleteThis is its slot 1.",
    },
    {
        "name": "ContactPointCallback",
        "class": "FOCollisionListener",
        "slot": 0,
        "se_target": 0x39C690,
        "ae_target": 0x3F4B90,
        "declaration": [
            ("include/RE/F/FOCollisionListener.h", "public bhkBackfaceContactListener"),
            ("include/RE/F/FOCollisionListener.h", "ContactPointCallback(const hkpContactPointEvent&) override;  // 00"),
            ("include/RE/B/bhkBackfaceContactListener.h", "public bhkContactListener"),
            ("include/RE/H/hkpContactListener.h", "virtual void ContactPointCallback(const hkpContactPointEvent&) {}   // 00"),
        ],
        "claim": "FOCollisionListener's primary base is bhkBackfaceContactListener and its override occupies inherited slot 0.",
    },
]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def address_library(path: Path, expected_version: tuple[int, int, int, int]) -> dict[int, int]:
    data = path.read_bytes()
    at = 0

    def read(fmt: str) -> int | tuple[int, ...]:
        nonlocal at
        size = struct.calcsize("<" + fmt)
        values = struct.unpack_from("<" + fmt, data, at)
        at += size
        return values[0] if len(values) == 1 else values

    if read("I") not in (1, 2):
        raise ValueError(f"Unsupported Address Library format: {path}")
    version = read("4I")
    name_size = read("I")
    at += name_size
    pointer_size, count = read("2I")
    result: dict[int, int] = {}
    previous_id = previous_offset = 0

    def decode(code: int, previous: int) -> int:
        if code == 0:
            return read("Q")
        if code == 1:
            return previous + 1
        if code == 2:
            return previous + read("B")
        if code == 3:
            return previous - read("B")
        if code == 4:
            return previous + read("H")
        if code == 5:
            return previous - read("H")
        if code == 6:
            return read("H")
        if code == 7:
            return read("I")
        raise ValueError(f"Invalid Address Library entry code {code}")

    for _ in range(count):
        kind = read("B")
        entry_id = decode(kind & 0x0F, previous_id)
        high = kind >> 4
        offset = decode(high & 0x07, previous_offset // pointer_size if high & 0x08 else previous_offset)
        if high & 0x08:
            offset *= pointer_size
        result[entry_id] = offset
        previous_id, previous_offset = entry_id, offset

    if at != len(data) or version != expected_version or pointer_size != 8:
        raise ValueError(f"Expected complete x64 Address Library {expected_version} in {path}")
    return result


class PEImage:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe : pe + 4] != b"PE\0\0":
            raise ValueError(f"Expected PE executable: {path}")
        machine, section_count, optional_size = struct.unpack_from("<HH12xH", self.data, pe + 4)
        if machine != 0x8664 or struct.unpack_from("<H", self.data, pe + 24)[0] != 0x20B:
            raise ValueError(f"Expected x64 PE32+ executable: {path}")
        self.image_base = struct.unpack_from("<Q", self.data, pe + 48)[0]
        self.sections: list[tuple[int, int, int, int]] = []
        for index in range(section_count):
            pos = pe + 24 + optional_size + index * 40
            _, rva, raw_size, raw = struct.unpack_from("<4I", self.data, pos + 8)
            characteristics = struct.unpack_from("<I", self.data, pos + 36)[0]
            self.sections.append((rva, raw_size, raw, characteristics))

    def read_rva(self, rva: int, length: int) -> bytes:
        for start, size, raw, _ in self.sections:
            if start <= rva and rva + length <= start + size:
                offset = raw + rva - start
                return self.data[offset : offset + length]
        raise ValueError(f"RVA {rva:#x} is not backed by file data in {self.path}")

    def is_executable_rva(self, rva: int) -> bool:
        return any(start <= rva < start + size and flags & 0x20000000 for start, size, _, flags in self.sections)


def get_vtable_id_pair(offsets_path: Path, name: str) -> tuple[int, int]:
    text = offsets_path.read_text(encoding="utf-8")
    match = re.search(rf"\bVTABLE_{re.escape(name)}\s*\{{([^}}]+)\}}", text)
    if not match:
        raise ValueError(f"Missing VTABLE_{name} in {offsets_path}")
    pair = re.search(r"REL::VariantID\(\s*(\d+)\s*,\s*(\d+)\s*,\s*0x[\da-fA-F]+\s*\)", match.group(1))
    if not pair:
        raise ValueError(f"Could not parse VTABLE_{name} SE/AE IDs")
    return int(pair.group(1)), int(pair.group(2))


def parse_source_contract(project_root: Path) -> list[dict]:
    header = project_root / "include" / "HookSites.h"
    text = header.read_text(encoding="utf-8")
    pattern = re.compile(
        r'CallSite\s+(\w+)\s*\{\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*'
        r'(0x[\da-fA-F]+|\d+)\s*,\s*(0x[\da-fA-F]+|\d+)\s*,\s*'
        r'(0x[\da-fA-F]+|\d+)\s*,\s*(0x[\da-fA-F]+|\d+)\s*\}',
        re.S,
    )
    parsed = []
    for symbol, name, se_id, ae_id, se_off, ae_off, se_target, ae_target in pattern.findall(text):
        parsed.append(
            {
                "symbol": symbol,
                "name": name,
                "se_id": int(se_id),
                "ae_id": int(ae_id),
                "se_offset": int(se_off, 0),
                "ae_offset": int(ae_off, 0),
                "se_target": int(se_target, 0),
                "ae_target": int(ae_target, 0),
            }
        )
    return parsed


def source_evidence(commonlib_root: Path, item: dict) -> list[dict]:
    evidence = []
    for relative, fragment in item["declaration"]:
        path = commonlib_root / relative
        lines = path.read_text(encoding="utf-8").splitlines()
        found = next((i for i, line in enumerate(lines, 1) if fragment in line), None)
        if found is None:
            raise ValueError(f"Missing CommonLib declaration {fragment!r} in {path}")
        evidence.append({"path": str(path), "line": found, "text": lines[found - 1].strip()})
    return evidence


def decode_window(image: PEImage, decoder: Cs, start_rva: int, end_rva: int, count: int = 4) -> list[dict]:
    data = image.read_rva(start_rva, end_rva - start_rva + 16)
    instructions = list(decoder.disasm(data, image.image_base + start_rva))
    call_addr = image.image_base + end_rva
    index = next((i for i, instruction in enumerate(instructions) if instruction.address == call_addr), None)
    if index is None:
        raise ValueError(f"RVA {end_rva:#x} is not an instruction boundary from function RVA {start_rva:#x}")
    return [
        {
            "rva": hex(instruction.address - image.image_base),
            "bytes": instruction.bytes.hex(" "),
            "mnemonic": instruction.mnemonic,
            "operands": instruction.op_str,
        }
        for instruction in instructions[max(0, index - count) : index + 3]
    ]


def disassembly(image: PEImage, decoder: Cs, rva: int, count: int = 6) -> list[dict]:
    return [
        {"rva": hex(instruction.address - image.image_base), "bytes": instruction.bytes.hex(" "), "mnemonic": instruction.mnemonic, "operands": instruction.op_str}
        for instruction in list(decoder.disasm(image.read_rva(rva, 48), image.image_base + rva))[:count]
    ]


def verify_runtime(
    name: str,
    version: tuple[int, int, int, int],
    executable: Path,
    library: Path,
    project_root: Path,
    commonlib_root: Path,
    offsets_path: Path,
    source_vfunc_checks: dict[str, str],
) -> dict:
    addresses = address_library(library, version)
    image = PEImage(executable)
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = False
    errors: list[str] = []
    calls = []
    for item in EXPECTED_CALLS:
        entry_id = item["se_id"] if name == "1.5.97" else item["ae_id"]
        offset = item["se_offset"] if name == "1.5.97" else item["ae_offset"]
        expected_target = item["se_target"] if name == "1.5.97" else item["ae_target"]
        function_rva = addresses.get(entry_id)
        if function_rva is None:
            errors.append(f"{item['name']}: Address Library lacks function ID {entry_id}")
            continue
        call_rva = function_rva + offset
        raw = image.read_rva(call_rva, 5)
        target_rva = call_rva + 5 + struct.unpack_from("<i", raw, 1)[0]
        boundary = True
        try:
            context = decode_window(image, decoder, function_rva, call_rva)
        except ValueError as error:
            boundary = False
            context = []
            errors.append(f"{item['name']}: {error}")
        is_call = raw[0] == 0xE8
        matches_target = target_rva == expected_target
        executable_target = image.is_executable_rva(target_rva)
        if not is_call:
            errors.append(f"{item['name']}: expected E8 rel32 at RVA {call_rva:#x}, found {raw[0]:02x}")
        if not matches_target:
            errors.append(f"{item['name']}: target RVA {target_rva:#x}, expected {expected_target:#x}")
        if not executable_target:
            errors.append(f"{item['name']}: target RVA {target_rva:#x} is not executable")
        if not boundary:
            continue
        calls.append(
            {
                "name": item["name"],
                "address_library_id": entry_id,
                "function_rva": hex(function_rva),
                "offset": hex(offset),
                "call_rva": hex(call_rva),
                "bytes": raw.hex(" "),
                "opcode_is_e8_rel32": is_call,
                "instruction_boundary_from_function_entry": boundary,
                "original_target_rva": hex(target_rva),
                "expected_target_rva": hex(expected_target),
                "target_is_executable": executable_target,
                "function_entry_disassembly": disassembly(image, decoder, function_rva, 8),
                "target_entry_disassembly": disassembly(image, decoder, target_rva),
                "call_context": context,
            }
        )

    vtables = []
    for item in EXPECTED_VTABLES:
        se_id, ae_id = get_vtable_id_pair(offsets_path, item["class"])
        table_id = se_id if name == "1.5.97" else ae_id
        table_rva = addresses.get(table_id)
        if table_rva is None:
            errors.append(f"{item['name']}: Address Library lacks vtable ID {table_id}")
            continue
        slot = item["slot"]
        raw_target = struct.unpack("<Q", image.read_rva(table_rva + slot * 8, 8))[0]
        target_rva = raw_target - image.image_base
        expected_target = item["se_target"] if name == "1.5.97" else item["ae_target"]
        is_exec = image.is_executable_rva(target_rva)
        if target_rva != expected_target:
            errors.append(f"{item['name']}: {item['class']} primary slot {slot} points to {target_rva:#x}, expected {expected_target:#x}")
        if not is_exec:
            errors.append(f"{item['name']}: vtable target {target_rva:#x} is not executable")
        source_check = source_vfunc_checks[item["class"]]
        hooks_text = (project_root / "include" / "Hooks.h").read_text(encoding="utf-8")
        if not re.search(source_check, hooks_text):
            errors.append(f"{item['class']} slot {slot}: source vtable install no longer matches checked subtable/slot")
        vtables.append(
            {
                "name": item["name"],
                "class": item["class"],
                "address_library_id": table_id,
                "primary_subtable_index": 0,
                "table_rva": hex(table_rva),
                "slot": slot,
                "byte_offset": hex(slot * 8),
                "pointer_value": hex(raw_target),
                "target_rva": hex(target_rva),
                "expected_target_rva": hex(expected_target),
                "target_is_executable": is_exec,
                "target_entry_disassembly": disassembly(image, decoder, target_rva),
                "commonlib_header_claim": item["claim"],
                "header_evidence": source_evidence(commonlib_root, item),
                "source_install_check": source_check,
            }
        )

    return {
        "runtime": name,
        "version": ".".join(map(str, version)),
        "executable": str(executable),
        "executable_sha256": sha256(executable),
        "address_library": str(library),
        "address_library_sha256": sha256(library),
        "image_base": hex(image.image_base),
        "calls": calls,
        "virtual_hooks": vtables,
        "errors": errors,
        "status": "PASS" if not errors and len(calls) == len(EXPECTED_CALLS) and len(vtables) == len(EXPECTED_VTABLES) else "FAIL",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=ROOTS / "AnimationMotionFix-master")
    parser.add_argument("--commonlib-root", type=Path, default=ROOTS / "CommonLibSSE-NG-ng")
    parser.add_argument("--exe-1597", type=Path, default=ROOTS / "valhallaCombat/build/abi/SkyrimSE-1.5.97.exe.unpacked.exe")
    parser.add_argument("--lib-1597", type=Path, default=ROOTS / "CommonLibSSE-NG-ng/tests/REL/version-1-5-97-0.bin")
    parser.add_argument("--exe-1170", type=Path, default=ROOTS / "valhallaCombat/build/abi/SkyrimSE-1.6.1170.exe.unpacked.exe")
    parser.add_argument("--lib-1170", type=Path, default=ROOTS / "CommonLibSSE-NG-ng/tests/REL/versionlib-1-6-1170-0.bin")
    parser.add_argument("--output", type=Path, default=None, help="Write JSON report (default: print to stdout)")
    args = parser.parse_args()

    expected = [{key: value for key, value in item.items() if key != "symbol"} for item in EXPECTED_CALLS]
    parsed = parse_source_contract(args.project_root)
    parsed_contract = [{key: value for key, value in item.items() if key != "symbol"} for item in parsed]
    source_errors = []
    if parsed_contract != expected:
        source_errors.append("include/HookSites.h call-site contract differs from the audited IDs, offsets, or targets")
    sites_cpp = (args.project_root / "src" / "HookSites.cpp").read_text(encoding="utf-8")
    preflight = re.search(r"for\s*\([^)]*:\s*\{([^}]+)\}\s*\)", sites_cpp, re.S)
    preflight_symbols = set(re.findall(r"\b[A-Z][A-Za-z0-9_]*\b", preflight.group(1))) if preflight else set()
    expected_symbols = {item["name"] for item in EXPECTED_CALLS}
    if preflight_symbols != expected_symbols:
        source_errors.append("src/HookSites.cpp preflight iteration does not cover exactly the seven audited CallSite constants")
    for required in ("site.seTargetRVA", "site.aeTargetRVA", "E8", "REL::Relocation<std::uintptr_t>"):
        if required not in sites_cpp:
            source_errors.append(f"src/HookSites.cpp no longer checks {required}")
    hooks_path = args.project_root / "include" / "Hooks.h"
    hooks_lines = hooks_path.read_text(encoding="utf-8").splitlines()
    stub_fragments = (
        "mov(r8, rdi)",
        "call(ptr[rip + hookLabel])",
        "jmp(ptr[rip + retnLabel])",
        "Hook_ConvertMoveDirToTranslation), target + 5",
        "call.ready()",
        "trampoline.allocate(call.getSize())",
        "std::memcpy(code, call.getCode(), call.getSize())",
        "trampoline.write_branch<5>(target, code)",
        "RE::Actor* a_actor",
    )
    stub_source = []
    for fragment in stub_fragments:
        line = next((i for i, text in enumerate(hooks_lines, 1) if fragment in text), None)
        if line is None:
            source_errors.append(f"include/Hooks.h pitch trampoline no longer contains {fragment}")
        else:
            stub_source.append({"path": str(hooks_path), "line": line, "text": hooks_lines[line - 1].strip()})
    source_vfunc_checks = {
        "bhkCharProxyController": r"VTABLE_bhkCharProxyController\s*\[\s*0\s*\][\s\S]{0,300}?write_vfunc\(\s*0x1\s*,\s*&Hook_ProcessConstraintsCallback",
        "bhkCharRigidBodyController": r"VTABLE_bhkCharRigidBodyController\s*\[\s*0\s*\][\s\S]{0,300}?write_vfunc\(\s*0x1\s*,\s*&Hook_DeleteThis",
        "FOCollisionListener": r"VTABLE_FOCollisionListener\s*\[\s*0\s*\][\s\S]{0,300}?write_vfunc\(\s*0x0\s*,\s*&Hook_ContactPointCallback",
    }
    offsets_path = args.commonlib_root / "include" / "RE" / "Offsets_VTABLE.h"
    runtimes = [
        verify_runtime("1.5.97", (1, 5, 97, 0), args.exe_1597, args.lib_1597, args.project_root, args.commonlib_root, offsets_path, source_vfunc_checks),
        verify_runtime("1.6.1170", (1, 6, 1170, 0), args.exe_1170, args.lib_1170, args.project_root, args.commonlib_root, offsets_path, source_vfunc_checks),
    ]
    for runtime in runtimes:
        runtime["errors"] = source_errors + runtime["errors"]
        if runtime["errors"]:
            runtime["status"] = "FAIL"
    report = {
        "schema": "animation-motion-fix-hook-abi-audit/v1",
        "status": "PASS" if all(runtime["status"] == "PASS" for runtime in runtimes) else "FAIL",
        "source_contract": str(args.project_root / "include" / "HookSites.h"),
        "source_contract_verified": not source_errors,
        "source_errors": source_errors,
        "runtimes": runtimes,
        "pitch_stub_abi": {
            "source_evidence": stub_source,
            "register_assessment": "The source stub leaves RCX and RDX untouched, copies RDI into R8, calls the hook, then tail-jumps to target+5. Under Win64 x64 calling convention R8 is a volatile argument register and RDI is nonvolatile; the register use is consistent with the declared third Actor* argument. The PE call windows show RCX/RDX stack-address setup and RDI-based field access before the original call, but do not by themselves prove RDI's C++ type.",
            "stack_assessment": "The generated stub makes a nested call and then jumps to the original continuation; the nested return balances its pushed return address. The stub itself emits no stack adjustment or explicit shadow-space reservation, so it relies on the original call site's Win64 pre-call alignment and 32-byte shadow area. The audit records function prologues and call windows but does not execute the code or prove every dynamic stack path.",
            "runtime_evidence": [
                {"runtime": runtime["runtime"], "pitch_translation_function_rva": next(call["function_rva"] for call in runtime["calls"] if call["name"] == "PitchTranslation"), "entry": next(call["function_entry_disassembly"] for call in runtime["calls"] if call["name"] == "PitchTranslation"), "call_context": next(call["call_context"] for call in runtime["calls"] if call["name"] == "PitchTranslation")}
                for runtime in runtimes
            ],
        },
        "evidence_scope": {
            "binary_proven": "Each listed call site is an E8 rel32 instruction at the decoded Address Library function RVA plus the declared offset; its resolved target equals the recorded executable RVA. Vtable bytes at the selected Address Library table and slot equal the recorded executable target.",
            "header_only": "Virtual method names/signatures, inheritance, primary-base offsets, and intended slot names are CommonLib declaration claims checked textually. The verifier does not prove C++ runtime object layout or dynamic semantic behavior from headers.",
            "limits": "Static PE/Address Library evidence does not prove runtime instance vptrs, call frequency, game-state behavior, thread safety, or the complete ABI of non-exported functions. For PitchTranslation, the hook's r8=rdi actor mapping is a source and call-site-register inference, not a standalone type proof from machine code.",
        },
    }
    output = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    else:
        sys.stdout.write(output)
    print(
        "AnimationMotionFix dual-runtime audit: "
        + report["status"]
        + f" ({sum(len(runtime['calls']) for runtime in runtimes)} calls, {sum(len(runtime['virtual_hooks']) for runtime in runtimes)} virtual entries)",
        file=sys.stderr,
    )
    if report["status"] != "PASS":
        for runtime in runtimes:
            for error in runtime["errors"]:
                print(f"{runtime['runtime']}: {error}", file=sys.stderr)
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
