apply(from = "shared/gradle/android-settings.gradle")

rootProject.name = "KairoTowns"
include(":kairotowns", ":frontend")
project(":frontend").projectDir = file("shared/frontend")
