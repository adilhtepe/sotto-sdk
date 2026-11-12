// Standalone build of the VBAN SDK Android library. Apps can also include
// it with: includeBuild("<path>/SDK/android") or include(":vban") + projectDir.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "vban-sdk-android"
include(":vban")
