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
    override val bridge by lazy { NativeMachine("kairotowns") }
    override val keyboard = TownsKeys.layout
    override val maxKeyCode = TownsKeys.MAX_CODE
    override fun guestKey(androidKey: Int) = TownsKeys.fromAndroid(androidKey)
    override val joystickControls get() = TownsKeys.joystick
    override val controllerSpec = ControllerGuestSpec("FM Towns", TownsKeys.bindableCodes, { TownsKeys.label(it) ?: "Key $it" },
        setOf(TownsKeys.SHIFT, TownsKeys.CTRL, TownsKeys.ALT), TownsKeys.joystick.zip(TownsKeys.joystickLabels),
        UiText.res(R.string.joystick_help), ControllerGuestSpec.STANDARD_ACTIONS, TownsKeys.layout)
    override val bindingsCodec = TownsKeys.bindings
    override val artworkDownloadsEnabled: Boolean get() = resources.getBoolean(R.bool.catalog_art_download_enabled)

    /**
     * CDs as Tsugaru opens them: a CCD reads its IMG, an MDS its MDF, a CUE the BIN, IMG or ISO
     * beside it, and an ISO itself. A CUE beside a CCD or MDS, and an ISO beside a CUE, are the
     * same disc, listed once. Floppies in D77 and raw sector images (HDM), and SCSI hard disks
     * (.h0 to .h6, the SCSI ID in the extension). tools/catalog_config.py hashes them by the same rules.
     */
    private val format = DiskImageLibrary.Format(
        kinds = listOf("ccd", "cue", "mds", "iso").associateWith { "cd" } +
            listOf("d77", "d88", "88d", "hdm").associateWith { "fd" } + (0..6).associate { "h$it" to "hd" },
        labels = mapOf("cd" to UiText.res(FrontendR.string.kairo_cd_rom), "fd" to UiText.res(FrontendR.string.kairo_floppy_disk),
            "hd" to UiText.res(FrontendR.string.kairo_hard_disk)),
        maxBytes = mapOf("cd" to 2L * 1024 * 1024 * 1024, "fd" to 64L * 1024 * 1024, "hd" to 4L * 1024 * 1024 * 1024),
        guestName = "FM Towns",
        data = mapOf("ccd" to listOf("img"), "mds" to listOf("mdf"), "cue" to listOf("bin", "img", "iso")),
        supersededBy = mapOf("cue" to setOf("ccd", "mds"), "iso" to setOf("cue")))
    private val disks by lazy {
        // 2: hard disks and HDM floppies, so archives scanned before are looked into again.
        DiskImageLibrary(this, format, DiskImageLibrary.Cache(mediaFormatsVersion = 2))
    }
    override fun createCatalog() = LayeredGameCatalog(this, TownsCatalog)
    override fun createLibrary(catalog: LayeredGameCatalog) = disks

    private fun isCd(entry: DiskImageEntry) = format.kind(entry.displayName) == "cd"
    private fun isFloppy(entry: DiskImageEntry) = format.kind(entry.displayName) == "fd"
    private fun isHardDisk(entry: DiskImageEntry) = format.kind(entry.displayName) == "hd"

    override val machineOptions = TownsCatalog.options
    // In the core's slot order (kairotowns/src/main/cpp/towns_core.cpp).
    override val mediaSlots = listOf(MediaSlot<DiskImageEntry>(UiText.res(FrontendR.string.kairo_cd_rom), ::isCd),
        MediaSlot(UiText.res(R.string.floppy_a), ::isFloppy), MediaSlot(UiText.res(R.string.floppy_b), ::isFloppy),
        MediaSlot(UiText.res(FrontendR.string.kairo_hard_disk), ::isHardDisk))

    private val firmwareDir by lazy { File(filesDir, "firmware") }
    override val firmwareSets by lazy {
        fun rom(name: String, label: UiText, description: UiText, required: Boolean, size: Int) =
            FirmwareSet.Part(name, label, UiText.res(FrontendR.string.kairo_firmware_part, name, description), setOf(name.lowercase()),
                required, setOf(size.toLong()))
        fun rom(name: String, label: Int, description: Int, required: Boolean, size: Int) =
            rom(name, UiText.res(label), UiText.res(description), required, size)
        listOf(UiText.res(FrontendR.string.kairo_firmware_title, "FM Towns") to FirmwareSet(firmwareDir, listOf(
            rom("FMT_SYS.ROM", UiText.res(R.string.firmware_system), UiText.res(R.string.firmware_system_description, "FM Towns"),
                true, 0x40000),
            rom("FMT_DOS.ROM", UiText.fixed("DOS"), UiText.res(R.string.firmware_dos_description), true, 0x80000),
            rom("FMT_FNT.ROM", R.string.firmware_font, R.string.firmware_font_description, true, 0x40000),
            rom("FMT_DIC.ROM", FrontendR.string.kairo_firmware_dictionary, R.string.firmware_dictionary_description, true, 0x80000),
            rom("FMT_F20.ROM", R.string.firmware_large_font, R.string.firmware_large_font_description, false, 0x80000))))
    }

    override fun startGame(entry: DiskImageEntry, game: CatalogGame, cancelled: AtomicBoolean,
                           progress: (String) -> Unit) {
        requireFirmware()
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
        val home = File(filesDir, "tsugaru").apply { mkdirs() }
        // The machine boots the disk the player starts: a hard disk boots before the CD beside it.
        val options = optionValues(game)
        val settings = options + (BOOT_HARD_DISK to if (isHardDisk(entry)) "on" else "off") +
            StartupCommands.setting(TownsCatalog.startupCommands.value(game, options))
        check(bridge.start(firmwareDir.absolutePath, home.absolutePath, images, settings)) {
            "Another machine is still running"
        }
        awaitRunning()
        mediaStarted(media)
    }

    private companion object {
        /** The launch setting that boots the hard disk although a CD is in the drive (towns_core.cpp). */
        const val BOOT_HARD_DISK = "bootHardDisk"
    }
}
