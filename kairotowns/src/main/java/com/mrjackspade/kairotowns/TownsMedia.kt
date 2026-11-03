package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.DiskImageLibrary
import com.mrjackspade.kairo.frontend.R as FrontendR
import com.mrjackspade.kairo.frontend.UiText

/** The library's media format, as the app's launch builds it (Kairo#125). */
object TownsMedia {
    /**
     * CDs as Tsugaru opens them: a CCD reads its IMG, an MDS its MDF, a CUE the BIN, IMG or ISO
     * beside it, and an ISO itself. A CUE beside a CCD or MDS, and an ISO beside a CUE, are the
     * same disc, listed once. Floppies in D77 and raw sector images (HDM), and SCSI hard disks
     * (.h0 to .h6, the SCSI ID in the extension). A BIN, IMG or ISO no sheet reads is a floppy when it
     * holds an FM Towns boot sector and is a floppy's size (bootFloppy): a boot disk a dump kept as
     * .bin beside its CD (KairoTowns#103). tools/catalog_config.py hashes them by the same rules.
     */
    val format = DiskImageLibrary.Format(
        kinds = listOf("ccd", "cue", "mds", "iso", "chd").associateWith { "cd" } +
            listOf("d77", "d88", "88d", "hdm").associateWith { "fd" } + (0..6).associate { "h$it" to "hd" },
        labels = mapOf("cd" to UiText.res(FrontendR.string.kairo_cd_rom), "fd" to UiText.res(FrontendR.string.kairo_floppy_disk),
            "hd" to UiText.res(FrontendR.string.kairo_hard_disk)),
        maxBytes = mapOf("cd" to 2L * 1024 * 1024 * 1024, "fd" to 64L * 1024 * 1024, "hd" to 4L * 1024 * 1024 * 1024),
        guestName = "FM Towns",
        data = mapOf("ccd" to listOf("img"), "mds" to listOf("mdf"), "cue" to listOf("bin", "img", "iso")),
        supersededBy = mapOf("cue" to setOf("ccd", "mds"), "iso" to setOf("cue")),
        probe = { head, size -> if (bootFloppy(head, size)) "fd" else null })

    /**
     * A floppy's raw sectors: the FM Towns boot sector's "IPL4" at the start, and at most 2 MB (a
     * 1.44 MB disk with a dump's padding; a CD's first sector has the same signature, but a CD is
     * far larger). tools/catalog_config.py's boot_floppy is the same rule.
     */
    private fun bootFloppy(head: ByteArray, size: Long) =
        size in 1..2L * 1024 * 1024 && head.size >= 4 && String(head, 0, 4, Charsets.US_ASCII) == "IPL4"
}
