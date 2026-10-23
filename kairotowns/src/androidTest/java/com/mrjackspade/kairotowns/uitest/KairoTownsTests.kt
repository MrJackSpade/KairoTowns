package com.mrjackspade.kairotowns.uitest

import com.mrjackspade.kairo.uitest.AppUnderTest
import com.mrjackspade.kairo.uitest.AppUnderTest.BiosFile
import com.mrjackspade.kairo.uitest.AppUnderTest.MediaSwap
import com.mrjackspade.kairo.uitest.KairoUiTest
import com.mrjackspade.kairo.uitest.suites.FirstRunTests
import com.mrjackspade.kairo.uitest.suites.GameTests
import com.mrjackspade.kairo.uitest.suites.LibraryTests
import com.mrjackspade.kairo.uitest.suites.SearchTests
import com.mrjackspade.kairo.uitest.suites.WordingTests
import com.mrjackspade.kairo.uitest.suites.MediaTests
import com.mrjackspade.kairo.uitest.suites.MenuTests
import com.mrjackspade.kairo.uitest.suites.ControllerTests
import com.mrjackspade.kairo.uitest.suites.TouchTests
import com.mrjackspade.kairo.uitest.suites.LaunchTests
import com.mrjackspade.kairo.uitest.suites.LibraryManagementTests
import com.mrjackspade.kairo.uitest.suites.SettingsTests
import com.mrjackspade.kairo.uitest.suites.StateTests
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
    override val arrowKeys = listOf(com.mrjackspade.kairotowns.TownsKeys.UP, com.mrjackspade.kairotowns.TownsKeys.DOWN,
        com.mrjackspade.kairotowns.TownsKeys.LEFT, com.mrjackspade.kairotowns.TownsKeys.RIGHT)
    override val returnKey = com.mrjackspade.kairo.frontend.JisMainBlock.RETURN
    override val spaceKey = com.mrjackspade.kairo.frontend.JisMainBlock.SPACE
    override val bootGameFile = "games/Populous & The Promised Lands (1990)(Imagineer)(Jp).zip"
    override val otherGame = "Intruder"
    override val defaultOption = "cpu"
    override val mediaSwap = MediaSwap("Intruder", "Floppy A", "Intruder")
}

class KairoTownsFirstRunTests : FirstRunTests() { override val app = KairoTownsApp }
class KairoTownsLibraryTests : LibraryTests() { override val app = KairoTownsApp }
class KairoTownsSearchTests : SearchTests() { override val app = KairoTownsApp }
class KairoTownsGameTests : GameTests() { override val app = KairoTownsApp }
class KairoTownsWordingTests : WordingTests() { override val app = KairoTownsApp }
class KairoTownsMediaTests : MediaTests() { override val app = KairoTownsApp }
class KairoTownsMenuTests : MenuTests() { override val app = KairoTownsApp }
class KairoTownsControllerTests : ControllerTests() { override val app = KairoTownsApp }
class KairoTownsTouchTests : TouchTests() { override val app = KairoTownsApp }
class KairoTownsLaunchTests : LaunchTests() { override val app = KairoTownsApp }
class KairoTownsLibraryManagementTests : LibraryManagementTests() { override val app = KairoTownsApp }
class KairoTownsSettingsTests : SettingsTests() { override val app = KairoTownsApp }
class KairoTownsStateTests : StateTests() { override val app = KairoTownsApp }

/** The FM Towns's own behavior: a CD game starts with its floppy in drive A. */
class FmTownsTests : KairoUiTest() {
    override val app = KairoTownsApp

    private fun startedWith(option: String): Pair<String, String?> {
        screens.completeFirstRun().waitForGame(KairoTownsApp.bootGame)
        val picked = screens.choose(screens.openMachinePage(), "default-$option")
        driver.press(android.view.KeyEvent.KEYCODE_BUTTON_MODE)
        screens.library.open(KairoTownsApp.bootGame).play()
        return picked to driver.probe().details["machine.$option"]
    }

    @Test
    fun theCpuClockReachesTheMachine() {
        val (picked, started) = startedWith("cpu")
        expect.equal("the CPU clock the FM Towns started with", picked, started)
    }

    @Test
    fun theMemoryReachesTheMachine() {
        val (picked, started) = startedWith("memory")
        expect.equal("the memory the FM Towns started with", picked, started)
    }

    @Test
    fun aCdGameStartsWithItsUserDiskInFloppyA() {
        screens.completeFirstRun().open("Intruder").play()
        val probe = driver.probe()
        expect.that("the CD drive holds Intruder's CD", probe.details["media.CD-ROM"].orEmpty().isNotEmpty()) { probe.details.toString() }
        expect.that("floppy A holds Intruder's user disk", probe.details["media.Floppy A"].orEmpty().isNotEmpty()) { probe.details.toString() }
    }
}
