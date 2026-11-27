plugins {
    id("com.android.application")
}

val abiList = (project.findProperty("abis") as String? ?: "arm64-v8a")
    .split(",").map { it.trim() }.filter { it.isNotEmpty() }

android {
    namespace = "io.github.alttprecomp"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.github.alttprecomp"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0-android"
        ndk {
            abiFilters += abiList
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DANDROID_STL=c++_static",
                )
                // Optional pre-extracted SDL3 tree (skips the download):
                // ./gradlew assembleDebug -Psdl3.dir=C:/path/SDL3-3.4.10
                // Local testing only: link the AOT tree generated from your own
                // ROM (src/gen). Never distribute such an APK.
                val aot = (project.findProperty("zelda.aot") as String?) == "true"
                arguments += "-DZELDA_INTERP_ONLY=" + (if (aot) "OFF" else "ON")
                (project.findProperty("sdl3.dir") as String?)?.let {
                    arguments += "-DSNESRECOMP_SDL3_SOURCE_DIR=$it"
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildTypes {
        debug {
            // The game core is heavy: keep native code optimized even in debug.
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs { useLegacyPackaging = true }
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }
}
