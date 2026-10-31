plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

extra["kairoProduct"] = "kairotowns"
extra["kairoLabel"] = "KairoTowns"
extra["kairoChd"] = true
extra["kairoCatalogRules"] = "com.mrjackspade.kairotowns.TownsCatalog"
apply(from = rootProject.file("shared/gradle/kairo-app.gradle"))
