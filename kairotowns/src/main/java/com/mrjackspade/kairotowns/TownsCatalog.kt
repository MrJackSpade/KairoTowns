package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.frontend.ChoiceOption
import com.mrjackspade.kairo.frontend.LayeredGameCatalog
import com.mrjackspade.kairo.frontend.MachineOption
import org.json.JSONArray

/**
 * KairoTowns's catalog records beyond the common fields: the machine a game boots (`machine`:
 * `memory` in megabytes, `cpu` clock in MHz) and a boot floppy for drive A (media role `floppyA`)
 * for CD games that start from one.
 */
object TownsCatalog : LayeredGameCatalog.Schema {
    override val platform = "fmtowns"
    override val updateUrl = "https://raw.githubusercontent.com/MrJackSpade/KairoTowns/main/catalog/core-v3.json"
    override val metadataUrl = "https://raw.githubusercontent.com/MrJackSpade/KairoTowns/main/catalog/core-v3.meta.json"
    override val coreVersion = 3
    override val selfHostedArt = "/MrJackSpade/KairoTowns/main/catalog/artwork/art/catalog/"
    override val contentId = Regex("sha256-(?:cd|fd)-v1:[0-9a-f]{64}")
    override val mediaExtensions = Regex("\\.(?:zip|7z|ccd|cue|mds|iso|img|bin|mdf|d77|d88|88d)$", RegexOption.IGNORE_CASE)

    val memory = MachineOption("memory", "Memory", listOf(2, 4, 6, 8, 12, 16).map { ChoiceOption("$it", "$it MB") },
        "6", numeric = true)
    val cpu = MachineOption("cpu", "CPU clock", listOf(16, 20, 25, 33, 40, 66).map { ChoiceOption("$it", "$it MHz") },
        "40", numeric = true)
    val skipMemoryCheck = MachineOption.skipMemoryCheck()
    val options = listOf(memory, cpu, skipMemoryCheck)

    override val fields: Map<String, Set<String>?> = mapOf(MachineOption.SECTION to options.map { it.id }.toSet())
    override val singleMediaRoles = setOf("floppyA")

    override fun validMedia(role: String, contentId: String) = role == "floppyA" && contentId.startsWith("sha256-fd-v1:")

    override fun validField(field: String, value: Any): Boolean =
        field == MachineOption.SECTION && MachineOption.validSection(options, value)

    override fun validBindings(bindings: JSONArray) = TownsKeys.bindings.valid(bindings)
}
