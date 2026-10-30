package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.R as FrontendR
import com.mrjackspade.kairo.frontend.CatalogSetting
import com.mrjackspade.kairo.frontend.ChoiceOption
import com.mrjackspade.kairo.frontend.LayeredGameCatalog
import com.mrjackspade.kairo.frontend.MachineOption
import com.mrjackspade.kairo.frontend.UiText
import org.json.JSONArray
import com.mrjackspade.kairo.frontend.JisMainBlock

/**
 * KairoTowns's catalog records beyond the common fields: the machine a game boots (`machine`:
 * `memory` in megabytes, `cpu` clock in MHz), a boot floppy for drive A (media role `floppyA`)
 * for CD games that start from one, the CD of a game that starts from its hard disk (media role
 * `cd`), and the lines typed at DOS's prompt to start the game (`launch`: `commands`).
 */
object TownsCatalog : LayeredGameCatalog.Schema {
    override val platform = "fmtowns"
    override val catalogUrl = "https://raw.githubusercontent.com/MrJackSpade/KairoTowns/main/catalog/"
    override val selfHostedArt = "/MrJackSpade/KairoTowns/main/catalog/artwork/art/catalog/"
    override val contentId = Regex("sha256-(?:cd|fd|hd)-v1:[0-9a-f]{64}")
    /** The FM Towns keyboard's Esc, and its Return and keypad Return. */
    override val escapeKeys = setOf(JisMainBlock.ESC)
    override val enterKeys = setOf(JisMainBlock.RETURN, TownsKeys.NUM_RETURN)
    override val mediaExtensions = Regex("\\.(?:zip|7z|ccd|cue|mds|iso|img|bin|mdf|d77|d88|88d|hdm|h[0-6])$", RegexOption.IGNORE_CASE)

    val memory = MachineOption("memory", UiText.res(FrontendR.string.kairo_option_memory),
        listOf(2, 4, 6, 8, 12, 16).map { ChoiceOption("$it", UiText.res(FrontendR.string.kairo_memory_mb, it)) }, "6", numeric = true)
    val cpu = MachineOption("cpu", UiText.res(FrontendR.string.kairo_option_clock),
        listOf(16, 20, 25, 33, 40, 66).map { ChoiceOption("$it", UiText.res(FrontendR.string.kairo_clock_mhz, it)) }, "40", numeric = true)
    val skipMemoryCheck = MachineOption.skipMemoryCheck()
    val options = listOf(memory, cpu, skipMemoryCheck)

    /** The lines typed at DOS's prompt to start the game, such as a hard disk's `CD \GAME` and `GAME`. */
    val startupCommands = CatalogSetting.Lines(LAUNCH, COMMANDS, UiText.res(FrontendR.string.kairo_startup_commands),
        UiText.res(FrontendR.string.kairo_startup_commands_hint), Regex("[ -~]{1,120}"), 8) { _, _ -> emptyList() }

    override val settings: List<CatalogSetting> = options.map(CatalogSetting::Choice) +
        CatalogSetting.Drive("floppyA", UiText.res(R.string.floppy_a)) { it.startsWith(CD) } +
        CatalogSetting.Drive("cd", UiText.res(FrontendR.string.kairo_cd_rom)) { it.startsWith(HARD_DISK) } + startupCommands +
        // A game's startup menus and disk swaps (shared StartupAutomation).
        listOf(CatalogSetting.StartupChoices,
        // The CD-ROM drive and floppy drives A and B (MainActivity.mediaSlots).
        CatalogSetting.DiskSwaps(3) { drive, id -> id.startsWith(if (drive == 0) CD else FLOPPY) })
    override val fields = CatalogSetting.fields(settings)
    /** Tsugaru moves the guest's TBIOS mouse pointer to a point (TownsMachine::mouse_warp). */
    override val directTouch = true

    override fun validMedia(role: String, contentId: String) =
        role == "floppyA" && contentId.startsWith(FLOPPY) || role == "cd" && contentId.startsWith(CD)

    override fun validField(field: String, value: Any): Boolean =
        field in fields && CatalogSetting.valid(settings, contentId, field, value)

    override fun validBindings(bindings: JSONArray) = TownsKeys.bindings.valid(bindings)

    const val LAUNCH = "launch"
    const val COMMANDS = "commands"
    private const val CD = "sha256-cd-v1:"
    private const val FLOPPY = "sha256-fd-v1:"
    private const val HARD_DISK = "sha256-hd-v1:"
}
