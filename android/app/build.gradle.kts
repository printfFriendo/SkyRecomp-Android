plugins {
    id("com.android.application")
}

android {
    namespace = "com.apolinario.skyrecomp"
    compileSdk = 34
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.apolinario.skyrecomp"
        minSdk = 29
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_ARM_NEON=ON",
                    "-DANDROID_PLATFORM=android-33"
                )
                abiFilters += listOf("arm64-v8a")
            }
        }
    }

    val keystoreFile = file("../skyrecomp-release.jks")
    if (keystoreFile.exists()) {
        signingConfigs {
            create("release") {
                storeFile = keystoreFile
                storePassword = "skyrecomp"
                keyAlias = "skyrecomp"
                keyPassword = "skyrecomp"
            }
        }
    }

    buildTypes {
        release {
            if (keystoreFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
            isJniDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
        }
    }
}

dependencies {
    implementation(files("libs/SDL3.aar"))
    implementation("androidx.core:core:1.13.1")
}
