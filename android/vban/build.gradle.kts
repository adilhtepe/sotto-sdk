import java.util.Properties

plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

// Coordinates so an app can use this build with includeBuild(...) and
// implementation("vban.sdk:vban:1.0.0") (Gradle substitutes the project).
group = "vban.sdk"
version = "1.0.0"

android {
    namespace = "vban.sdk"
    compileSdk = 34
    // NDK: set `vban.ndkPath` in gradle.properties / -P, or install one in the SDK.
    (findProperty("vban.ndkPath") as String?)?.let { path ->
        ndkPath = path
        // AGP insists ndkVersion matches the NDK at ndkPath: read it from there
        val props = Properties()
        file("$path/source.properties").inputStream().use { props.load(it) }
        ndkVersion = props.getProperty("Pkg.Revision").substringBefore("-")
    }

    defaultConfig {
        minSdk = 21
        consumerProguardFiles("consumer-rules.pro")
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=none")
            }
        }
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64", "x86") }
    }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    testOptions { unitTests.isReturnDefaultValues = true }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}

// ---- JVM unit tests run the real JNI library, built for the host ----------
val hostJniDir = layout.buildDirectory.dir("hostJni")
val buildHostJni by tasks.registering(Exec::class) {
    group = "verification"
    description = "Builds libvban_jni for the host JVM (used by the unit tests)."
    val src = file("src/main/cpp")
    val out = hostJniDir.get().asFile
    inputs.dir(src)
    inputs.dir(file("../../src"))
    outputs.dir(out)
    commandLine("sh", "-c",
        "cmake -S '$src' -B '$out' -DCMAKE_BUILD_TYPE=Release > /dev/null && cmake --build '$out' -j")
}
tasks.withType<Test>().configureEach {
    dependsOn(buildHostJni)
    systemProperty("java.library.path", hostJniDir.get().asFile.absolutePath)
    testLogging { events("passed", "failed"); showStandardStreams = true; exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL }
}
