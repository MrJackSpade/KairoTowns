package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.CatalogSetting
import com.mrjackspade.kairo.frontend.ChoiceOption
import com.mrjackspade.kairo.frontend.LayeredGameCatalog
import com.mrjackspade.kairo.frontend.MachineOption
import org.json.JSONArray

/**
 * KairoTowns's catalog records beyond the common fields: the machine a game boots (`machine`:
 * `memory` in megabytes, `cpu` clock in MHz), a boot floppy for drive A (media role `floppyA`)
 * for CD games that start from one, the CD of a game that starts from its hard disk (media role
 * `cd`), and the lines typed at DOS's prompt to start the game (`launch`: `commands`).
 */
object TownsCatalog : LayeredGameCatalog.Schema {
    override val platform = "fmtowns"
    override val updateUrl = "https://raw.githubusercontent.com/MrJackSpade/KairoTowns/main/catalog/core-v3.json"
    override val metadataUrl = "https://raw.githubusercontent.com/MrJackSpade/KairoTowns/main/catalog/core-v3.meta.json"
    override val coreVersion = 3
    override val selfHostedArt = "/MrJackSpade/KairoTowns/main/catalog/artwork/art/catalog/"
    override val contentId = Regex("sha256-(?:cd|fd|hd)-v1:[0-9a-f]{64}")
    override val mediaExtensions = Regex("\\.(?:zip|7z|ccd|cue|mds|iso|img|bin|mdf|d77|d88|88d|hdm|h[0-6])$", RegexOption.IGNORE_CASE)

    val memory = MachineOption("memory", "Memory", listOf(2, 4, 6, 8, 12, 16).map { ChoiceOption("$it", "$it MB") },
        "6", numeric = true)
    val cpu = MachineOption("cpu", "CPU clock", listOf(16, 20, 25, 33, 40, 66).map { ChoiceOption("$it", "$it MHz") },
        "40", numeric = true)
    val skipMemoryCheck = MachineOption.skipMemoryCheck()
    val options = listOf(memory, cpu, skipMemoryCheck)

    /** The lines typed at DOS's prompt to start the game, such as a hard disk's `CD \GAME` and `GAME`. */
    val startupCommands = CatalogSetting.Lines(LAUNCH, COMMANDS, "Startup commands", "Lines to type, separated by ;",
        Regex("[ -~]{1,120}"), 8) { _, _ -> emptyList() }

    override val settings: List<CatalogSetting> = options.map(CatalogSetting::Choice) +
        CatalogSetting.Drive("floppyA", "Floppy A") { it.startsWith(CD) } +
        CatalogSetting.Drive("cd", "CD-ROM") { it.startsWith(HARD_DISK) } + startupCommands
    override val fields = CatalogSetting.fields(settings)

    override fun validMedia(role: String, contentId: String) =
        role == "floppyA" && contentId.startsWith(FLOPPY) || role == "cd" && contentId.startsWith(CD)

    override fun validField(field: String, value: Any): Boolean =
        field in fields && CatalogSetting.validSection(settings, field, value)

    override fun validBindings(bindings: JSONArray) = TownsKeys.bindings.valid(bindings)

    const val LAUNCH = "launch"
    const val COMMANDS = "commands"
    private const val CD = "sha256-cd-v1:"
    private const val FLOPPY = "sha256-fd-v1:"
    private const val HARD_DISK = "sha256-hd-v1:"
}
