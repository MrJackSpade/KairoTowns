package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.FirmwareSet
import com.mrjackspade.kairo.frontend.R as FrontendR
import com.mrjackspade.kairo.frontend.UiText
import java.io.File

/** The firmware the app's Machine page lists, as its launch builds it (Kairo#125). */
object TownsFirmware {
    /** Where the Marty's chips are, below the firmware [directory]. */
    fun martyDirectory(directory: File) = File(directory, "marty")

    /** Each set, titled, with its files in [directory]. */
    fun sets(directory: File): List<Pair<UiText, FirmwareSet>> {
        fun rom(name: String, label: UiText, description: UiText, required: Boolean, size: Int) =
            FirmwareSet.Part(name, label, UiText.res(FrontendR.string.kairo_firmware_part, name, description), setOf(name.lowercase()),
                required, setOf(size.toLong()))
        fun rom(name: String, label: Int, description: Int, required: Boolean, size: Int) =
            rom(name, UiText.res(label), UiText.res(description), required, size)
        return listOf(UiText.res(FrontendR.string.kairo_firmware_title, "FM Towns") to FirmwareSet(directory, listOf(
            rom("FMT_SYS.ROM", UiText.res(R.string.firmware_system), UiText.res(R.string.firmware_system_description, "FM Towns"),
                true, 0x40000),
            rom("FMT_DOS.ROM", UiText.fixed("DOS"), UiText.res(R.string.firmware_dos_description), true, 0x80000),
            rom("FMT_FNT.ROM", R.string.firmware_font, R.string.firmware_font_description, true, 0x40000),
            rom("FMT_DIC.ROM", FrontendR.string.kairo_firmware_dictionary, R.string.firmware_dictionary_description, true, 0x80000),
            rom("FMT_F20.ROM", R.string.firmware_large_font, R.string.firmware_large_font_description, false, 0x80000))),
            // The FM Towns Marty's own ROM chips (KairoTowns#96), MAME's dumps, found by content;
            // MartyRoms cuts them into the files Tsugaru reads.
            UiText.res(FrontendR.string.kairo_firmware_title, "FM Towns Marty") to FirmwareSet(martyDirectory(directory), (0..1).map { index ->
                val name = if (index == 0) MartyRoms.CHIP0 else MartyRoms.CHIP1
                FirmwareSet.Part(name, UiText.res(R.string.firmware_marty_chip, index + 1),
                    UiText.res(FrontendR.string.kairo_firmware_part, name, UiText.res(R.string.firmware_marty_chip_description)),
                    setOf(name), true, setOf(MartyRoms.CHIP_BYTES.toLong()), setOf(MartyRoms.SHA1.getValue(name)))
            }, note = UiText.res(R.string.firmware_marty_note)))
    }
}
