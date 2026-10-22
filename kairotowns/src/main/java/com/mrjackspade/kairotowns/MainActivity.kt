package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.CatalogGame
import com.mrjackspade.kairo.frontend.ControllerGuestSpec
import com.mrjackspade.kairo.frontend.ControllerLayout
import com.mrjackspade.kairo.frontend.DiskImageEntry
import com.mrjackspade.kairo.frontend.DiskImageLibrary
import com.mrjackspade.kairo.frontend.FirmwareSet
import com.mrjackspade.kairo.frontend.LayeredGameCatalog
import com.mrjackspade.kairo.frontend.LibraryStrings
import com.mrjackspade.kairo.frontend.MachineActivity
import com.mrjackspade.kairo.frontend.MachineFirstRun
import com.mrjackspade.kairo.frontend.NativeMachine
import com.mrjackspade.kairo.frontend.mediaFor

import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/** KairoTowns: the FM Towns on Tsugaru, in the shared Kairo frontend. */
class MainActivity : MachineActivity<DiskImageEntry>() {
    override val identity = Identity("KAIROTOWNS", 0xff3AF677.toInt(), "KairoTowns", "FM Towns", "kairotowns",
        LibraryStrings("KAIROTOWNS", "KairoTowns"),
        "KairoTowns uses the Tsugaru emulator. Source and provenance: github.com/MrJackSpade/KairoTowns.",
        "Choose a games folder to find CD and floppy images and the ZIP and 7z archives holding them",
        "Folder access expired. Select the games folder again.",
        MachineFirstRun.Folder("Choose a games folder", "Select the folder containing your FM Towns games. KairoTowns " +
            "will scan its CD and floppy images and the ZIP and 7z archives holding them.", "Choose games folder…"),
        "SAVE STATES", "MrJackSpade/KairoTowns")
    override val machine = Machine(640, 480, 4.0 / 3.0, "state.tss")
    override val bridge by lazy { NativeMachine("kairotowns") }
    override val keyboard = TownsKeys.layout
    override val maxKeyCode = TownsKeys.MAX_CODE
    override fun guestKey(androidKey: Int) = TownsKeys.fromAndroid(androidKey)
    override val joystickControls get() = TownsKeys.joystick
    override val controllerSpec = ControllerGuestSpec("FM Towns", TownsKeys.bindableCodes, { TownsKeys.label(it) ?: "Key $it" },
        setOf(TownsKeys.SHIFT, TownsKeys.CTRL, TownsKeys.ALT), TownsKeys.joystick.zip(TownsKeys.joystickLabels),
        "The gamepad in game port 1.", ControllerGuestSpec.STANDARD_ACTIONS, TownsKeys.layout)
    override val bindingsCodec = TownsKeys.bindings
    override fun defaultBindings(layout: ControllerLayout) = TownsKeys.defaults(layout)
    override val artworkDownloadsEnabled: Boolean get() = resources.getBoolean(R.bool.catalog_art_download_enabled)

    /**
     * CDs as Tsugaru opens them: a CCD reads its IMG, an MDS its MDF, a CUE the BIN, IMG or ISO
     * beside it, and an ISO itself. A CUE beside a CCD or MDS, and an ISO beside a CUE, are the
     * same disc, listed once. tools/catalog_config.py hashes them by the same rules.
     */
    private val disks by lazy {
        DiskImageLibrary(this, DiskImageLibrary.Format(
            kinds = listOf("ccd", "cue", "mds", "iso").associateWith { "cd" } + listOf("d77", "d88", "88d").associateWith { "fd" },
            labels = mapOf("cd" to "CD-ROM", "fd" to "Floppy disk"),
            maxBytes = mapOf("cd" to 2L * 1024 * 1024 * 1024, "fd" to 64L * 1024 * 1024),
            guestName = "FM Towns",
            data = mapOf("ccd" to listOf("img"), "mds" to listOf("mdf"), "cue" to listOf("bin", "img", "iso")),
            supersededBy = mapOf("cue" to setOf("ccd", "mds"), "iso" to setOf("cue"))))
    }
    override fun createCatalog() = LayeredGameCatalog(this, TownsCatalog)
    override fun createLibrary(catalog: LayeredGameCatalog) = disks

    private fun isCd(entry: DiskImageEntry) = entry.mediaLabel == "CD-ROM"

    override val machineOptions = TownsCatalog.options
    override val mediaPageTitle = "Discs"
    override val mediaSlots = listOf(MediaSlot<DiskImageEntry>("CD-ROM", ::isCd),
        MediaSlot("Floppy A") { !isCd(it) }, MediaSlot("Floppy B") { !isCd(it) })

    private val firmwareDir by lazy { File(filesDir, "firmware") }
    override val firmwareSets by lazy {
        fun rom(name: String, label: String, description: String, required: Boolean, size: Int) =
            FirmwareSet.Part(name, label, "$name · $description", setOf(name.lowercase()), required, setOf(size.toLong()))
        listOf("FM Towns BIOS" to FirmwareSet(firmwareDir, listOf(
            rom("FMT_SYS.ROM", "System", "the BIOS that starts the FM Towns", true, 0x40000),
            rom("FMT_DOS.ROM", "DOS", "the built-in DOS that CD games boot through", true, 0x80000),
            rom("FMT_FNT.ROM", "Font", "the text and kanji font", true, 0x40000),
            rom("FMT_DIC.ROM", "Dictionary", "kana-to-kanji dictionary for Japanese text", true, 0x80000),
            rom("FMT_F20.ROM", "Large font", "a 20-dot font some games use", false, 0x80000))))
    }

    override fun startGame(entry: DiskImageEntry, game: CatalogGame, cancelled: AtomicBoolean,
                           progress: (String) -> Unit) {
        requireFirmware()
        val floppy = when {
            !isCd(entry) -> entry
            else -> game.mediaId("floppyA")?.let { id ->
                mediaFor(id, entry, libraryEntries) ?: error("The boot floppy this game needs is not in the library")
            }
        }
        val cd = entry.takeIf(::isCd)
        val disc = cd?.let { disks.prepare(it, cancelled, progress) }
        val floppyImage = floppy?.let { disks.prepare(it, cancelled, progress) }
        val home = File(filesDir, "tsugaru").apply { mkdirs() }
        check(bridge.start(firmwareDir.absolutePath, home.absolutePath,
            listOf(disc?.absolutePath, floppyImage?.absolutePath, null),
            optionValues(game))) { "Another machine is still running" }
        awaitRunning()
        mediaStarted(listOf(cd, floppy, null))
    }

    override fun stateError(code: Int) = when (code) {
        2 -> "the emulator could not read or write the state"
        else -> "code $code"
    }
}
