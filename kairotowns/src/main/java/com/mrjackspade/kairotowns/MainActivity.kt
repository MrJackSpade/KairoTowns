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
import com.mrjackspade.kairo.frontend.StartupCommands
import com.mrjackspade.kairo.frontend.UiText
import com.mrjackspade.kairo.frontend.companionFor
import com.mrjackspade.kairo.frontend.missingDisk
import com.mrjackspade.kairo.frontend.R as FrontendR

import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/** KairoTowns: the FM Towns on Tsugaru, in the shared Kairo frontend. */
class MainActivity : MachineActivity<DiskImageEntry>() {
    override val identity = Identity("KAIROTOWNS", 0xff3AF677.toInt(), "KairoTowns", "FM Towns", "kairotowns",
        LibraryStrings("KAIROTOWNS", "KairoTowns"),
        UiText.res(R.string.credits, "KairoTowns", "Tsugaru", "MrJackSpade/KairoTowns"),
        UiText.res(R.string.library_empty), UiText.res(FrontendR.string.kairo_library_expired),
        MachineFirstRun.Folder(UiText.res(FrontendR.string.kairo_folder_title), UiText.res(R.string.folder_description, "FM Towns", "KairoTowns"),
            UiText.res(FrontendR.string.kairo_folder_button)),
        UiText.res(FrontendR.string.kairo_saves_group), "MrJackSpade/KairoTowns")
    override val machine = Machine(640, 480, 4.0 / 3.0, "state.tss")
    override val notices = listOf("THIRD_PARTY_NOTICES.txt", "CHD_NOTICES.txt")
    override val bridge by lazy { NativeMachine("kairotowns", listOf(TownsKeys.CAPS, TownsKeys.KATAKANA)) }
    override val directTouchExplanation = UiText.res(R.string.touch_direct_explanation)
    override val keyboard = TownsKeys.layout
    override val maxKeyCode = TownsKeys.MAX_CODE
    override fun guestKey(androidKey: Int) = TownsKeys.fromAndroid(androidKey)
    override val joystickControls get() = TownsKeys.joystick
    override val controllerSpec = ControllerGuestSpec("FM Towns", TownsKeys.bindableCodes, { TownsKeys.label(it) ?: "Key $it" },
        setOf(TownsKeys.SHIFT, TownsKeys.CTRL, TownsKeys.ALT), TownsKeys.joystick.zip(TownsKeys.joystickLabels),
        UiText.res(R.string.joystick_help), ControllerGuestSpec.STANDARD_ACTIONS, TownsKeys.layout, TownsKeys.twoButtonPad)
    override val bindingsCodec = TownsKeys.bindings
    override val artworkDownloadsEnabled: Boolean get() = resources.getBoolean(R.bool.catalog_art_download_enabled)

    private val format = TownsMedia.format
    private val disks by lazy {
        // 2: hard disks and HDM floppies, 3: CHDs, 4: multi-BIN CUEs, 5: boot floppies kept as track data,
        // so archives scanned before are looked into again.
        DiskImageLibrary(this, format, DiskImageLibrary.Cache(mediaFormatsVersion = 5))
    }
    override fun createCatalog() = LayeredGameCatalog(this, TownsCatalog)
    override fun createLibrary(catalog: LayeredGameCatalog) = disks

    private fun isCd(entry: DiskImageEntry) = entry.kind == "cd"
    private fun isFloppy(entry: DiskImageEntry) = entry.kind == "fd"
    private fun isHardDisk(entry: DiskImageEntry) = entry.kind == "hd"

    override val machineOptions = TownsCatalog.options
    // The core holds each button change for a few guest frames (KairoTowns#99), so a tap is a short click and two taps
    // are a double click, as Towns MENU needs to open anything.
    override val tapClickMillis: Long = 100

    // A zero-filled 1.2 MB floppy (.hdm), which Towns OS's Disk › Initialize formats, and zero-filled
    // SCSI hard disks at ID 0 (.h0), which Towns OS's hard disk setup prepares.
    override val blankDisks = listOf(coreBlankDisk("fd-2hd", UiText.res(FrontendR.string.kairo_blank_floppy, "1.2 MB · 2HD"), "hdm")) +
        listOf(40, 100, 240).map { mb ->
            coreBlankDisk("scsi-$mb", UiText.res(FrontendR.string.kairo_blank_hard_disk, "$mb MB · SCSI"), "h0")
        }
    // In the core's slot order (kairotowns/src/main/cpp/towns_core.cpp).
    override val mediaSlots = listOf(MediaSlot<DiskImageEntry>(UiText.res(FrontendR.string.kairo_cd_rom), ::isCd),
        MediaSlot(UiText.res(R.string.floppy_a), ::isFloppy), MediaSlot(UiText.res(R.string.floppy_b), ::isFloppy),
        MediaSlot(UiText.res(FrontendR.string.kairo_hard_disk), ::isHardDisk))

    private val firmwareDir by lazy { File(filesDir, "firmware") }
    override val firmwareSets by lazy { TownsFirmware.sets(firmwareDir) }
    private val martyDir by lazy { TownsFirmware.martyDirectory(firmwareDir) }
    private fun isMarty(options: Map<String, String>) = options.getValue(TownsCatalog.model.id) == "marty"

    /** A game with the 6-button pad plays with its mapping: L and R are C and Z (catalog/system-v2.json). */
    override fun machineControllerLayers(options: Map<String, String>) =
        if (options.getValue(TownsCatalog.pad.id) == "6") listOf(SIX_BUTTON_PAD_LAYER) else emptyList()

    override fun startGame(entry: DiskImageEntry, game: CatalogGame, cancelled: AtomicBoolean,
                           progress: (String) -> Unit) {
        val options = optionValues(game)
        // The Joystick tab shows the pad the game has (TownsCatalog.pad).
        val pad = TownsKeys.joystickPorts(options.getValue(TownsCatalog.pad.id))
        runOnUiThread { useJoystickPorts(pad) }
        // A Marty starts from its own ROMs, not the FM Towns's.
        requireFirmware(listOf(firmwareSets[if (isMarty(options)) 1 else 0].second))
        val romDir = if (isMarty(options)) MartyRoms.prepare(martyDir, File(martyDir, "rom")) else firmwareDir
        // A CD game's floppy: the boot floppy its catalog names, else the one floppy in its set, such
        // as a user disk.
        val floppy = when {
            isFloppy(entry) -> entry
            isCd(entry) -> companionFor(game.mediaId("floppyA"), entry, launchEntries, missingDisk(UiText.res(R.string.companion_boot_floppy)), ::isFloppy)
            else -> null
        }
        // A hard disk game's CD, and the hard disk installed beside a CD, as their sets hold them.
        val cd = if (isCd(entry)) entry else companionFor(game.mediaId("cd"), entry, launchEntries, missingDisk(UiText.res(FrontendR.string.kairo_companion_cd)), ::isCd)
        val hardDisk = if (isHardDisk(entry)) entry else companionFor(null, entry, launchEntries, missingDisk(UiText.res(R.string.companion_hard_disk)), ::isHardDisk)
        val media = listOf(cd, floppy, null, hardDisk)
        val images = media.map { disk -> disk?.let { disks.prepare(it, cancelled, progress).absolutePath } }
        // A Marty keeps a CMOS of its own, apart from the FM Towns games' (towns_core.cpp, <home>/cmos.bin).
        val home = File(filesDir, if (isMarty(options)) "tsugaru-marty" else "tsugaru").apply { mkdirs() }
        // The machine boots the disk the player starts: a hard disk boots before the CD beside it.
        val settings = options + (BOOT_HARD_DISK to if (isHardDisk(entry)) "on" else "off") +
            StartupCommands.setting(TownsCatalog.startupCommands.value(game, options))
        check(bridge.start(romDir.absolutePath, home.absolutePath, images, settings)) {
            "Another machine is still running"
        }
        awaitRunning()
        mediaStarted(media)
    }

    private companion object {
        /** The launch setting that boots the hard disk although a CD is in the drive (towns_core.cpp). */
        const val BOOT_HARD_DISK = "bootHardDisk"
        /** The system definition's controller layer for the 6-button pad (LayeredGameCatalog.systemControllerLayer). */
        const val SIX_BUTTON_PAD_LAYER = "sixButtonPad"
    }
}
