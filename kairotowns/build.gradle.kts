plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

extra["kairoProduct"] = "kairotowns"
extra["kairoLabel"] = "KairoTowns"
apply(from = rootProject.file("shared/gradle/kairo-machine-app.gradle"))
