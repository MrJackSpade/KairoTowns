#!/usr/bin/env python3
"""Writes core/test/towns/mouse.bin, the shared PC boot test's FM Towns mouse program disk
(KairoTowns#90): a 1232 KB floppy with TOWNSMS.COM, 8086 code for MS-DOS that reads the mouse in
game port B itself (04D2h, its four nibbles of -dx and -dy clocked by COM, 04D6h bit 5), with
interrupts off, so nothing but it sees the motion: a guest with no TBIOS mouse (KairoTowns#110).
It draws its pointer, an 8 x 8 block, in the FMR-compatible graphics VRAM (C0000h, 80 bytes a line,
the planes written chosen at CFF81h), moving a pixel per count over 640 x 400 and stopping at the
edges, once per frame (CFF86h bit 2, vertical sync). A press of the left button fills the screen
with a colour from where the pointer is: 1 + (x / 128 + 5 * (y / 128)) mod 14. It never ends.

    make_towns_mouse_disk.py
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "shared/tools"))
from fat_disk import add_file, floppy_2hd  # noqa: E402
from x86_assembler import JE, JGE, JLE, JNZ, JZ, LOOP, Assembler  # noqa: E402

OUTPUT = ROOT / "core/test/towns/mouse.bin"
PORT_B, PORT_OUT = 0x04D2, 0x04D6
# Game port output: both ports' TRIG lines high (their buttons read through), and COM for port B.
COM_HIGH, COM_LOW = 0x2F, 0x0F
VRAM_SEGMENT, MASK, SYNC = 0xC000, 0xFF81, 0xFF86
LINE, LINES = 80, 400
POINTER_COLOUR = 15


def program():
    a = Assembler()
    b, w = a.b, a.w

    def out_port(value):
        b(0xBA); w(PORT_OUT)                    # mov dx, 04D6h
        b(0xB0, value, 0xEE)                    # mov al, value; out dx, al

    def in_port():
        b(0xBA); w(PORT_B)                      # mov dx, 04D2h
        b(0xEC)                                 # in al, dx

    def mask(register_or_value):
        """mov byte es:[CFF81h], value (an immediate), or ah (None)."""
        if register_or_value is None:
            b(0x26, 0x88, 0x26); w(MASK)        # mov es:[MASK], ah
        else:
            b(0x26, 0xC6, 0x06); w(MASK); b(register_or_value)

    b(0xFA, 0xFC)                               # cli; cld
    b(0xB8); w(VRAM_SEGMENT); b(0x8E, 0xC0)     # es = VRAM
    b(0xB4, 0x00); a.call("fill")  # call fill (black)
    b(0xC7, 0x06); w("x"); w(320)
    b(0xC7, 0x06); w("y"); w(200)
    b(0xC7, 0x06); w("prev"); w(0xFFFF)
    b(0xC6, 0x06); w("button"); b(0x10)         # released
    b(0xC6, 0x06); w("background"); b(0)

    a.label("frame")
    a.label("leave")
    b(0x26, 0xF6, 0x06); w(SYNC); b(0x04); a.jcc(JNZ, "leave")   # test byte es:[SYNC], 4
    a.label("enter")
    b(0x26, 0xF6, 0x06); w(SYNC); b(0x04); a.jcc(JZ, "enter")

    # The four nibbles: X high (COM high), X low (low), Y high (high), Y low (low).
    b(0xB1, 4)                                  # mov cl, 4
    out_port(COM_HIGH); in_port(); b(0x24, 0x0F, 0xD2, 0xE0, 0x88, 0xC3)   # bl = X high << 4
    out_port(COM_LOW); in_port(); b(0xA2); w("now")                        # [now]: bit 4 the left button, 0 pressed
    b(0x24, 0x0F, 0x08, 0xC3, 0xF6, 0xDB)                                  # bl |= X low; neg bl
    out_port(COM_HIGH); in_port(); b(0x24, 0x0F, 0xD2, 0xE0, 0x88, 0xC7)   # bh = Y high << 4
    out_port(COM_LOW); in_port(); b(0x24, 0x0F, 0x08, 0xC7, 0xF6, 0xDF)    # bh |= Y low; neg bh

    def axis(reg_to_al, var, limit, done):
        b(0x88, reg_to_al)                      # mov al, bl / bh
        b(0x98)                                 # cbw
        b(0x03, 0x06); w(var)                   # add ax, [var]
        b(0x85, 0xC0); a.jcc(JGE, done + "_low")  # test ax, ax
        b(0x31, 0xC0)                           # xor ax, ax
        a.label(done + "_low")
        b(0x3D); w(limit); a.jcc(JLE, done)     # cmp ax, limit
        b(0xB8); w(limit)
        a.label(done)
        b(0xA3); w(var)                         # mov [var], ax

    axis(0xD8, "x", 639, "x_done")
    axis(0xF8, "y", LINES - 1, "y_done")

    # The pointer's place: min(y, 392) * 80 + x / 8.
    b(0xA1); w("y"); b(0x3D); w(LINES - 8); a.jcc(JLE, "top_ok")
    b(0xB8); w(LINES - 8)
    a.label("top_ok")
    b(0xBB); w(LINE); b(0xF7, 0xE3, 0x89, 0xC7)              # di = ax * 80
    b(0xA1); w("x"); b(0xB1, 3, 0xD3, 0xE8, 0x01, 0xC7)      # di += x >> 3
    b(0x3B, 0x3E); w("prev"); a.jcc(JE, "drawn")             # cmp di, [prev]
    b(0x57)                                                  # push di
    b(0x8B, 0x3E); w("prev"); b(0x83, 0xFF, 0xFF); a.jcc(JE, "unsaved")  # mov di,[prev]; cmp di,-1
    b(0x8A, 0x26); w("background"); a.call("block")
    a.label("unsaved")
    b(0x5F)                                                  # pop di
    b(0x89, 0x3E); w("prev")
    b(0xB4, POINTER_COLOUR); a.call("block")
    a.label("drawn")

    # A press of the left button: the screen filled with the pointer's colour, the pointer over it.
    b(0xA0); w("now"); b(0x24, 0x10)                         # al = [now] & 10h
    b(0x3A, 0x06); w("button"); a.jcc(JE, "next")            # cmp al, [button]
    b(0xA2); w("button"); b(0x84, 0xC0); a.jcc(JNZ, "next")  # released: nothing
    b(0xA1); w("x"); b(0xB1, 7, 0xD3, 0xE8, 0x89, 0xC3)      # bx = x >> 7
    b(0xA1); w("y"); b(0xD3, 0xE8)                           # ax = y >> 7
    b(0xB2, 5, 0xF6, 0xE2, 0x01, 0xD8)                       # ax = ax * 5 + bx
    b(0xB3, 14, 0xF6, 0xF3, 0x88, 0xE0, 0xFE, 0xC0)          # al = ax % 14 + 1
    b(0xA2); w("background"); b(0x88, 0xC4)                  # [background] = al; ah = al
    a.call("fill")
    b(0x8B, 0x3E); w("prev"); b(0xB4, POINTER_COLOUR)
    a.call("block")
    a.label("next")
    a.jmp_near("frame")

    # fill: the whole 640 x 400 screen in colour ah.
    a.label("fill")
    mask(0x0F); b(0x31, 0xFF, 0x30, 0xC0, 0xB9); w(LINE * LINES); b(0xF3, 0xAA)   # all planes 0
    mask(None); b(0x31, 0xFF, 0xB0, 0xFF, 0xB9); w(LINE * LINES); b(0xF3, 0xAA)   # ah's planes 1
    b(0xC3)

    # block: 8 pixels by 8 lines from es:di in colour ah.
    a.label("block")
    b(0x57, 0xB9); w(8)                                      # push di; mov cx, 8
    a.label("block_line")
    mask(0x0F); b(0x26, 0xC6, 0x05, 0x00)                    # mov byte es:[di], 0
    mask(None); b(0x26, 0xC6, 0x05, 0xFF)                    # mov byte es:[di], 0FFh
    b(0x83, 0xC7, LINE); a.jcc(LOOP, "block_line")           # add di, 80
    b(0x5F, 0xC3)                                            # pop di; ret

    for name, size in (("x", 2), ("y", 2), ("prev", 2), ("button", 1), ("background", 1), ("now", 1)):
        a.label(name)
        b(*([0] * size))
    return a.build()


def main(argv):
    if len(argv) != 1:
        raise SystemExit(__doc__)
    code = program()
    image = floppy_2hd()
    add_file(image, 0, "TOWNSMS.COM", code)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(bytes(image))
    print(f"{OUTPUT}: TOWNSMS.COM, {len(code)} bytes")


if __name__ == "__main__":
    main(sys.argv)
