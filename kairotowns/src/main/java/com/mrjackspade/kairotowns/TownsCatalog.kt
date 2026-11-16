package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.R as FrontendR
import com.mrjackspade.kairo.frontend.CatalogSetting
import com.mrjackspade.kairo.frontend.ChoiceOption
import com.mrjackspade.kairo.frontend.CustomValue
import com.mrjackspade.kairo.frontend.LayeredGameCatalog
import com.mrjackspade.kairo.frontend.MachineOption
import com.mrjackspade.kairo.frontend.UiText
import org.json.JSONArray
import com.mrjackspade.kairo.frontend.JisMainBlock

/**
 * KairoTowns's catalog records beyond the common fields: the machine a game boots (`machine`:
 * `model`, `memory` in megabytes, `cpu` clock in MHz), a boot floppy for drive A (media role `floppyA`)
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
    override val mediaExtensions = Regex("\\.(?:zip|7z|ccd|cue|mds|iso|chd|img|bin|mdf|d77|d88|88d|hdm|h[0-6])$", RegexOption.IGNORE_CASE)

    /**
     * Main RAM in MB: the common sizes, or any whole MB typed (Kairo#183) up to what Tsugaru gives a
     * machine (towns.cpp: 64 MB, 10 MB on the Marty's 386SX).
     */
    val memory = MachineOption("memory", UiText.res(FrontendR.string.kairo_option_memory),
        { listOf(2, 4, 6, 8, 12, 16).map { ChoiceOption("$it", UiText.res(FrontendR.string.kairo_memory_mb, it)) } }, "6", numeric = true,
        customWith = { values ->
            CustomValue.range(1, if (values["model"] == "marty") 10 else 64, 1) { UiText.res(FrontendR.string.kairo_memory_mb, it) }
        })
    /** The CPU clock in MHz: the models' own, or any whole MHz typed (Kairo#183), which Tsugaru runs at as given. */
    val cpu = MachineOption("cpu", UiText.res(FrontendR.string.kairo_option_clock),
        { listOf(16, 20, 25, 33, 40, 66).map { ChoiceOption("$it", UiText.res(FrontendR.string.kairo_clock_mhz, it)) } }, "40", numeric = true,
        customWith = { CustomValue.range(1, 100, 1) { UiText.res(FrontendR.string.kairo_clock_mhz, it) } })
    val skipMemoryCheck = MachineOption.skipMemoryCheck()
    /**
     * The FM Towns or the FM Towns Marty (KairoTowns#96): the catalog marks Marty games, and a game
     * no catalog knows is asked at its first launch.
     */
    val model = MachineOption("model", UiText.res(R.string.model),
        listOf(ChoiceOption("towns", UiText.fixed("FM Towns")), ChoiceOption("marty", UiText.fixed("FM Towns Marty"))), "towns",
        question = UiText.res(R.string.model_question))
    /**
     * The gamepad in game port 0 (KairoTowns#104): the 2-button pad (the Marty's own on a Marty), or
     * Fujitsu's 6-button pad, which the catalog gives the games written for it. Not the default for
     * every game: a game that drives the pad's select line high loses the 6-button pad's directions.
     */
    val pad = MachineOption("pad", UiText.res(R.string.pad),
        listOf(ChoiceOption("2", UiText.res(R.string.pad_2)), ChoiceOption("6", UiText.res(R.string.pad_6))), "2")
    /**
     * Tsugaru's support for one game (KairoTowns#77, its TOWNS_APPSPECIFIC settings): how it reads that game's
     * own mouse pointer, its 386DX check, its flight-stick axes and the like. The catalog gives it the games
     * Tsugaru has a setting for (tools/catalog_config.py); every other game runs with none.
     */
    val app = MachineOption("app", UiText.res(R.string.game_support),
        listOf(ChoiceOption("none", UiText.res(FrontendR.string.kairo_none))) + listOf(
            ChoiceOption("WINGCOMMANDER1", UiText.fixed("Wing Commander")),
            ChoiceOption("WINGCOMMANDER2", UiText.fixed("Wing Commander II")),
            ChoiceOption("SUPERDAISEN", UiText.fixed("Super Daisenryaku")),
            ChoiceOption("LEMMINGS", UiText.fixed("Lemmings")),
            ChoiceOption("LEMMINGS2", UiText.fixed("Lemmings 2")),
            ChoiceOption("STRIKECOMMANDER", UiText.fixed("Strike Commander")),
            ChoiceOption("AMARANTH3", UiText.fixed("Amaranth III")),
            ChoiceOption("ULTIMAUNDERWORLD", UiText.fixed("Ultima Underworld")),
            ChoiceOption("OPERATIONWOLF", UiText.fixed("Operation Wolf")),
            ChoiceOption("BRANDISH", UiText.fixed("Brandish")),
            ChoiceOption("AIRWARRIORV2", UiText.fixed("Air Warrior V2")),
            ChoiceOption("DUNGEONMASTER_JP", UiText.fixed("Dungeon Master (日本語)")),
            ChoiceOption("DUNGEONMASTER_EN", UiText.fixed("Dungeon Master (English)")),
            ChoiceOption("DAIKOUKAIJIDAI", UiText.fixed("Daikoukai Jidai")),
            ChoiceOption("DAIKOUKAIJIDAI2", UiText.fixed("Daikoukai Jidai II")),
            ChoiceOption("RASHINBAN", UiText.fixed("Ougon no Rashinban")),
            ChoiceOption("AFTERBURNER2", UiText.fixed("After Burner")),
            ChoiceOption("ORGEL", UiText.fixed("Orgel")),
            ChoiceOption("ROCKETRANGER", UiText.fixed("Rocket Ranger")),
            ChoiceOption("ASUKA120", UiText.fixed("Asuka 120%")),
            ChoiceOption("DRAKKEN", UiText.fixed("Drakken"))), "none")
    val options = listOf(model, memory, cpu, skipMemoryCheck, pad, app)

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
