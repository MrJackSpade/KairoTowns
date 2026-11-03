package com.mrjackspade.kairotowns

import com.mrjackspade.kairo.KanaLegendsTest
import com.mrjackspade.kairo.LaunchTablesTest
import java.io.File

/** What KairoTowns builds as it launches (Kairo#125, KairoTowns#86). */
class TownsLaunchTablesTest : LaunchTablesTest() {
    override fun tables() = LaunchTables(listOf(TownsMedia.format), listOf(TownsKeys.layout),
        TownsFirmware.sets(File("firmware")).map { it.second }, mapOf("controller bindings" to { TownsKeys.bindings }))
}

/** The FM Towns keyboard's kana legends (Kairo#126, KairoTowns#94). */
class TownsKanaLegendsTest : KanaLegendsTest() {
    override val layouts = listOf(TownsKeys.layout)
}
