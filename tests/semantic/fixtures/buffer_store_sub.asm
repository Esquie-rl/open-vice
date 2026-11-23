; Raw V# (OOB_SELECT 3) at 0x1000 with num_records 8. Dwords at 0x1000, 0x1004 start as 0xAAAAAAAA, 0xBBBBBBBB;
; the dword at 0x1008 (past the end) starts as 0xCCCCCCCC. v1 = 0x11223344.
; byte           offset 0: byte 0 = 0x44              -> dword 0 = 0xAAAAAA44
; byte_d16_hi    offset 1: byte 1 = 0x22 (bits 23:16) -> dword 0 = 0xAAAA2244
; short          offset 2: bytes 2,3 = 0x44,0x33      -> dword 0 = 0x33442244
; short_d16_hi   offset 3: bytes 3,4 = 0x22,0x11 (crosses into dword 1) -> dword 0 = 0x22442244, dword 1 = 0xBBBBBB11
; byte_d16_hi    offset 7: byte 7 = 0x22              -> dword 1 = 0x22BBBB11
; short offset 7 (7 + 2 > 8) and byte offset 8 are out of range and write nothing.
; Final words: 0x22442244, 0x22BBBB11, 0xCCCCCCCC.
s_mov_b32 s4, 0x1000
s_mov_b32 s5, 0
s_mov_b32 s6, 8
s_mov_b32 s7, 0x30014000
v_mov_b32 v1, 0x11223344
v_mov_b32 v2, 0
v_mov_b32 v3, 1
v_mov_b32 v4, 2
v_mov_b32 v5, 3
v_mov_b32 v6, 7
v_mov_b32 v7, 8
buffer_store_byte v1, v2, s[4:7], 0 offen
buffer_store_byte_d16_hi v1, v3, s[4:7], 0 offen
buffer_store_short v1, v4, s[4:7], 0 offen
buffer_store_short_d16_hi v1, v5, s[4:7], 0 offen
buffer_store_byte_d16_hi v1, v6, s[4:7], 0 offen
buffer_store_short v1, v6, s[4:7], 0 offen
buffer_store_byte v1, v7, s[4:7], 0 offen
s_endpgm
