package com.mrjackspade.kairotowns

import java.io.File
import java.io.RandomAccessFile

/**
 * The FM Towns Marty's ROMs (KairoTowns#96): its two 2 MB ROM chips as MAME dumps them
 * (fmtmarty.zip's mrom.m36 and mrom.m37), cut into the files Tsugaru reads from a ROM folder. The
 * Marty's system, DOS, font and dictionary ROMs are its own, not the FM Towns's, so a Marty
 * starts from a folder of its own; the four EX-ROMs alone on top of an FM Towns set do not boot.
 */
object MartyRoms {
    const val CHIP0 = "mrom.m36"
    const val CHIP1 = "mrom.m37"
    const val CHIP_BYTES = 0x200000

    /** MAME's dumps, by SHA-1. */
    val SHA1 = mapOf(CHIP0 to "5721c5f9657c570638352fa9acac57fa8d0b94bd", CHIP1 to "e273b5fa618373bdf7536495cd53c8aac1cce9a5")

    /** Each file Tsugaru reads: the chip, offset and length it is cut from. */
    private class Slice(val name: String, val chip: String, val offset: Int, val length: Int)

    private val slices = listOf(
        Slice("FMT_DOS.ROM", CHIP0, 0x000000, 0x80000),
        Slice("MAR_EX0.ROM", CHIP0, 0x080000, 0x80000),
        Slice("MAR_EX1.ROM", CHIP0, 0x100000, 0x80000),
        Slice("MAR_EX2.ROM", CHIP0, 0x180000, 0x80000),
        Slice("MAR_EX3.ROM", CHIP1, 0x000000, 0x80000),
        Slice("FMT_DIC.ROM", CHIP1, 0x100000, 0x80000),
        Slice("FMT_FNT.ROM", CHIP1, 0x180000, 0x40000),
        Slice("FMT_SYS.ROM", CHIP1, 0x1C0000, 0x40000))

    /**
     * [romDir] holding Tsugaru's files cut from the chips in [chipDir]: made again when a file is
     * missing or older than its chip, each written whole before it replaces the old one.
     */
    fun prepare(chipDir: File, romDir: File): File {
        check(romDir.isDirectory || romDir.mkdirs()) { "Cannot create the Marty ROM folder" }
        for (slice in slices) {
            val chip = File(chipDir, slice.chip)
            check(chip.length() == CHIP_BYTES.toLong()) { "${slice.chip} is not a Marty ROM chip" }
            val target = File(romDir, slice.name)
            if (target.isFile && target.lastModified() >= chip.lastModified()) continue
            val bytes = ByteArray(slice.length)
            RandomAccessFile(chip, "r").use { input ->
                input.seek(slice.offset.toLong())
                input.readFully(bytes)
            }
            val partial = File(romDir, "${slice.name}.part")
            partial.writeBytes(bytes)
            java.nio.file.Files.move(partial.toPath(), target.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING)
        }
        return romDir
    }
}
