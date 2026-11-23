"""Semantic interpreter tests for sub-dword MUBUF stores: buffer_store_byte, buffer_store_short and their _d16_hi forms.

  - tests/semantic/fixtures/buffer_store_sub.asm: one hand-written program, final words worked out by hand,
  - every op at byte offsets 0..7 of a raw V# (neighbouring bytes kept, dword-crossing shorts at offset 3),
  - OOB_SELECT modes 0/1/2/3 at the exact end of the access (a short at the last byte is out of range),
  - soffset, instruction offset, idxen, a lane that is off in EXEC, two lanes in one dword, a scratch-swizzled V#,
  - rejected forms (lds, tfe, d16, a multi-register data operand).

Standard library only; no GPU, no LLVM, no game files.
"""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from support import rdna2spirv, translate_lines  # noqa: E402
import spirv_mini_interp as si  # noqa: E402

BASE = 0x1000
FILL = 0xAAAAAAAA
SENTINEL = 0xCCCCCCCC
FORMAT = 20 << 12                       # V# FORMAT 32_UINT, word3 bits 18:12, as the other buffer tests use
DATA = 0x11223344
# op: (bytes stored, the data's byte at the lowest address)
OPS = {"buffer_store_byte": (1, 0x44), "buffer_store_byte_d16_hi": (1, 0x22),
       "buffer_store_short": (2, 0x44), "buffer_store_short_d16_hi": (2, 0x22)}
SHORT_SECOND = {"buffer_store_short": 0x33, "buffer_store_short_d16_hi": 0x11}


def fssm_blob():
    fssm = bytearray(0x148)
    struct.pack_into("<4I", fssm, 0x138, 0x88, 32, 1, 1)
    return bytes(fssm)


def run_program(lines, memory, nlanes=1):
    spv = translate_lines("cs_bufstore", lines + ["s_endpgm"], fssm=fssm_blob())
    mem = dict(memory)
    si.run_workgroup(spv, nlanes, 32,
                     inputs_for=lambda i: {"%wg_id": (0, 0, 0), "%local_id": (i, 0, 0), "%sg_lane": i},
                     push_constants=[0] * 36, memory=mem)
    return mem


def fresh(dwords=3):
    """FILL words, the last one SENTINEL (past the end of the 8-byte buffers used here)."""
    mem = {BASE + 4 * i: FILL for i in range(dwords)}
    mem[BASE + 4 * (dwords - 1)] = SENTINEL
    return mem


def vsharp(num_records, stride=0, oob=3, word1_flags=0, word3_extra=0):
    return [f"s_mov_b32 s4, {BASE:#x}", f"s_mov_b32 s5, {word1_flags | (stride & 0x3FFF) << 16:#x}",
            f"s_mov_b32 s6, {num_records:#x}", f"s_mov_b32 s7, {(oob << 28) | FORMAT | word3_extra:#x}"]


def store(op, offset, num_records=8, memory=None, stride=0, oob=3, idx=None, inst=""):
    body = vsharp(num_records, stride, oob) + [f"v_mov_b32_e32 v1, {DATA:#x}"]
    if idx is None:
        body += [f"v_mov_b32_e32 v2, {offset:#x}", f"{op} v1, v2, s[4:7], 0 offen {inst}"]
    else:
        body += [f"v_mov_b32_e32 v2, {idx:#x}", f"v_mov_b32_e32 v3, {offset:#x}",
                 f"{op} v1, v[2:3], s[4:7], 0 idxen offen {inst}"]
    return run_program(body, memory if memory is not None else fresh(3))


def expected_bytes(op, offset):
    """Bytes the store writes, as {byte offset: value}, from the ISA text (low 8 / 16 bits of the chosen half)."""
    nbytes, first = OPS[op]
    written = {offset: first}
    if nbytes == 2:
        written[offset + 1] = SHORT_SECOND[op]
    return written


def apply(base_words, written):
    raw = bytearray(b"".join(struct.pack("<I", w) for w in base_words))
    for off, value in written.items():
        raw[off] = value
    return [struct.unpack_from("<I", raw, 4 * i)[0] for i in range(len(raw) // 4)]


class FixtureTests(unittest.TestCase):
    def test_hand_written_program(self):
        path = Path(__file__).with_name("fixtures") / "buffer_store_sub.asm"
        lines = [ln for ln in path.read_text(encoding="ascii").splitlines() if ln and not ln.startswith(";")]
        spv = translate_lines("cs_bufstore_fixture", lines, fssm=fssm_blob())
        mem = {BASE: 0xAAAAAAAA, BASE + 4: 0xBBBBBBBB, BASE + 8: SENTINEL}
        si.run_workgroup(spv, 1, 32, inputs_for=lambda i: {"%wg_id": (0, 0, 0), "%local_id": (i, 0, 0), "%sg_lane": i},
                         push_constants=[0] * 36, memory=mem)
        self.assertEqual((mem[BASE], mem[BASE + 4], mem[BASE + 8]), (0x22442244, 0x22BBBB11, SENTINEL))


class OffsetTests(unittest.TestCase):
    def test_every_op_at_every_offset_keeps_neighbours(self):
        for op, (nbytes, _) in OPS.items():
            for off in range(0, 9 - nbytes):
                with self.subTest(op=op, off=off):
                    mem = store(op, off)
                    want = apply([FILL, FILL, SENTINEL], expected_bytes(op, off))
                    self.assertEqual([mem[BASE], mem[BASE + 4], mem[BASE + 8]], want)

    def test_known_words(self):
        self.assertEqual(store("buffer_store_byte", 1)[BASE], 0xAAAA44AA)
        self.assertEqual(store("buffer_store_byte_d16_hi", 2)[BASE], 0xAA22AAAA)
        self.assertEqual(store("buffer_store_short", 2)[BASE], 0x3344AAAA)
        self.assertEqual(store("buffer_store_short_d16_hi", 0)[BASE], 0xAAAA1122)
        crossed = store("buffer_store_short", 3)
        self.assertEqual((crossed[BASE], crossed[BASE + 4]), (0x44AAAAAA, 0xAAAAAA33))


class BoundsTests(unittest.TestCase):
    def test_raw_end_check_covers_the_whole_access(self):
        untouched = [FILL, FILL, SENTINEL]
        for op, (nbytes, first) in OPS.items():
            with self.subTest(op=op, off=7):
                mem = store(op, 7)
                got = [mem[BASE], mem[BASE + 4], mem[BASE + 8]]
                if nbytes == 1:                    # the last byte of the buffer is in range
                    self.assertEqual(got, [FILL, (first << 24) | 0x00AAAAAA, SENTINEL])
                else:                              # 7 + 2 > 8: nothing is written, not even byte 7
                    self.assertEqual(got, untouched)
            with self.subTest(op=op, off=8):
                mem = store(op, 8)
                self.assertEqual([mem[BASE], mem[BASE + 4], mem[BASE + 8]], untouched)

    def test_raw_edge_is_num_records(self):
        mem = store("buffer_store_short", 3, num_records=5)       # bytes 3..4 end exactly at num_records
        self.assertEqual((mem[BASE], mem[BASE + 4]), (0x44AAAAAA, 0xAAAAAA33))
        mem = store("buffer_store_short", 3, num_records=4)       # 3 + 2 > 4: nothing, not even byte 3
        self.assertEqual((mem[BASE], mem[BASE + 4]), (FILL, FILL))

    def test_structured_with_offset(self):
        # mode 0: out of range if idx >= num_records or off + size > stride
        mem = store("buffer_store_short", 2, num_records=2, stride=4, oob=0, idx=1)
        self.assertEqual((mem[BASE], mem[BASE + 4]), (FILL, 0x3344AAAA))       # byte 1 * 4 + 2 = 6
        mem = store("buffer_store_short", 3, num_records=2, stride=4, oob=0, idx=0)
        self.assertEqual((mem[BASE], mem[BASE + 4]), (FILL, FILL))              # 3 + 2 > stride 4
        mem = store("buffer_store_short", 0, num_records=1, stride=4, oob=0, idx=1)
        self.assertEqual((mem[BASE], mem[BASE + 4]), (FILL, FILL))              # idx 1 >= num_records 1
        mem = store("buffer_store_short", 2, num_records=2, stride=8, oob=0, idx=1, memory=fresh(4))
        self.assertEqual(mem[BASE + 8], 0x3344AAAA)                             # 1 * 8 + 2 = byte 10

    def test_structured_and_disabled_modes(self):
        # mode 1 checks idx only; mode 2 only num_records == 0
        self.assertEqual(store("buffer_store_byte", 1, num_records=1, stride=4, oob=1, idx=0)[BASE], 0xAAAA44AA)
        self.assertEqual(store("buffer_store_byte", 0, num_records=1, stride=4, oob=1, idx=1)[BASE], FILL)
        self.assertEqual(store("buffer_store_byte", 0, num_records=0, stride=4, oob=2, idx=0)[BASE], FILL)
        self.assertEqual(store("buffer_store_byte", 0, num_records=1, stride=4, oob=2, idx=0)[BASE], 0xAAAAAA44)


class AddressingTests(unittest.TestCase):
    def test_instruction_offset(self):
        mem = store("buffer_store_byte", 1, inst="offset:2")
        self.assertEqual(mem[BASE], 0x44AAAAAA)                   # 1 + 2 = byte 3

    def test_soffset_without_offen(self):
        body = vsharp(8) + [f"v_mov_b32_e32 v1, {DATA:#x}", "s_mov_b32 s8, 5",
                            "buffer_store_short v1, off, s[4:7], s8 offset:1"]
        mem = run_program(body, fresh(3))
        self.assertEqual((mem[BASE], mem[BASE + 4]), (FILL, 0x3344AAAA))        # 5 + 1 = byte 6

    def test_inactive_lane_writes_nothing(self):
        # lane l stores byte l; only lane 1 is on, so byte 0 keeps its value
        body = vsharp(8) + ["v_add_nc_u32_e32 v1, 0x41, v0",
                            "v_cmp_eq_u32_e32 vcc_lo, 1, v0", "s_and_saveexec_b32 s8, vcc_lo",
                            "buffer_store_byte v1, v0, s[4:7], 0 offen", "s_mov_b32 exec_lo, s8"]
        mem = run_program(body, fresh(3), nlanes=2)
        self.assertEqual(mem[BASE], 0xAAAA42AA)

    def test_two_lanes_share_a_dword(self):
        # the atomic and / or keeps the byte the other lane wrote
        body = vsharp(8) + ["v_add_nc_u32_e32 v1, 0x40, v0", "buffer_store_byte v1, v0, s[4:7], 0 offen"]
        mem = run_program(body, fresh(3), nlanes=2)
        self.assertEqual(mem[BASE], 0xAAAA4140)


class SwizzleTests(unittest.TestCase):
    def test_scratch_swizzle_crossing_short(self):
        # SWIZZLE_ENABLE, ADD_TID_ENABLE, INDEX_STRIDE 8, stride 16: lane l at offset 3 starts at byte l * 4 + 3, and
        # the second byte of a short is in the dword 4 * 8 bytes on, as buffer_load_ushort reads it.
        mem = {BASE + 4 * i: 0xEEEEEEEE for i in range(24)}
        body = vsharp(64, stride=16, word1_flags=1 << 31, word3_extra=1 << 23) + [
            "v_add_nc_u32_e32 v1, 0x1234, v0", "v_mov_b32_e32 v2, 3",
            "buffer_store_short v1, v2, s[4:7], 0 offen"]
        out = run_program(body, mem, nlanes=2)
        self.assertEqual((out[BASE], out[BASE + 4]), (0x34EEEEEE, 0x35EEEEEE))            # lane 0 at 3, lane 1 at 7
        self.assertEqual((out[BASE + 32], out[BASE + 36]), (0xEEEEEE12, 0xEEEEEE12))      # second bytes
        touched = (BASE, BASE + 4, BASE + 32, BASE + 36)
        self.assertTrue(all(out[a] == 0xEEEEEEEE for a in mem if a not in touched))


class RejectedFormTests(unittest.TestCase):
    def test_rejected_forms(self):
        pre = vsharp(8) + ["v_mov_b32_e32 v2, 0"]
        for line in ("buffer_store_short v1, v2, s[4:7], 0 offen lds",
                     "buffer_store_short v1, v2, s[4:7], 0 offen tfe",
                     "buffer_store_short v1, v2, s[4:7], 0 offen d16",
                     "buffer_store_byte v[1:2], v2, s[4:7], 0 offen"):
            with self.subTest(line=line), self.assertRaises(rdna2spirv.Unsupported):
                translate_lines("cs_bufstore_bad", pre + [line, "s_endpgm"], fssm=fssm_blob())

    def test_unknown_narrow_store_still_unsupported(self):
        with self.assertRaises(rdna2spirv.Unsupported):
            translate_lines("cs_bufstore_unk", vsharp(8) + ["buffer_store_byte_d16_lo v1, v2, s[4:7], 0 offen",
                                                            "s_endpgm"], fssm=fssm_blob())


if __name__ == "__main__":
    unittest.main()
