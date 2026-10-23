package com.mrjackspade.kairotowns.uitest

import com.mrjackspade.kairo.uitest.AppUnderTest
import com.mrjackspade.kairo.uitest.AppUnderTest.BiosFile
import com.mrjackspade.kairo.uitest.KairoUiTest
import com.mrjackspade.kairo.uitest.suites.FirstRunTests
import com.mrjackspade.kairo.uitest.suites.GameTests
import com.mrjackspade.kairo.uitest.suites.LibraryTests
import com.mrjackspade.kairo.uitest.suites.SearchTests
import com.mrjackspade.kairo.uitest.suites.WordingTests
import org.junit.Test

/**
 * KairoTowns as the shared UI tests see it. Its test resources are TOSEC CD archives: Populous &
 * The Promised Lands, and Intruder with its user disk, with the FM Towns system, DOS, font and
 * dictionary files.
 */
object KairoTownsApp : AppUnderTest() {
    override val name = "KairoTowns"
    override val brand = "KAIROTOWNS"
    override val biosTitle = "FM Towns BIOS"
    override val biosFiles = listOf(BiosFile("FMT_SYS.ROM", true), BiosFile("FMT_DOS.ROM", true),
        BiosFile("FMT_FNT.ROM", true), BiosFile("FMT_DIC.ROM", true), BiosFile("FMT_F20.ROM", false))
    override val bootGame = "Populous & The Promised Lands"
    override val singleEntryGames = listOf("Intruder")
    override val searchTerm = "Populous"
}

class KairoTownsFirstRunTests : FirstRunTests() { override val app = KairoTownsApp }
class KairoTownsLibraryTests : LibraryTests() { override val app = KairoTownsApp }
class KairoTownsSearchTests : SearchTests() { override val app = KairoTownsApp }
class KairoTownsGameTests : GameTests() { override val app = KairoTownsApp }
class KairoTownsWordingTests : WordingTests() { override val app = KairoTownsApp }

/** The FM Towns's own behavior: a CD game starts with its floppy in drive A. */
class FmTownsTests : KairoUiTest() {
    override val app = KairoTownsApp

    @Test
    fun aCdGameStartsWithItsUserDiskInFloppyA() {
        screens.completeFirstRun().open("Intruder").play()
        val probe = driver.probe()
        expect.that("the CD drive holds Intruder's CD", probe.details["media.CD-ROM"].orEmpty().isNotEmpty()) { probe.details.toString() }
        expect.that("floppy A holds Intruder's user disk", probe.details["media.Floppy A"].orEmpty().isNotEmpty()) { probe.details.toString() }
    }
}
