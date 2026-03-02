#! /usr/bin/env bash
#
# Copyright (C) 2014 Miguel Botón <waninkoko@gmail.com>
# Copyright (C) 2014 Zhang Rui <bbcallen@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#--------------------
set -e

if [ -z "$ANDROID_NDK" ]; then
    echo "You must define ANDROID_NDK before starting."
    echo "They must point to your NDK directories.\n"
    exit 1
fi

#--------------------
# common defines
FF_ARCH=$1
if [ -z "$FF_ARCH" ]; then
    echo "You must specific an architecture 'arm, armv7a, x86, ...'.\n"
    exit 1
fi


FF_BUILD_ROOT=`pwd`
FF_ANDROID_PLATFORM=android-9


FF_BUILD_NAME=
FF_SOURCE=
FF_CROSS_PREFIX=

FF_CFG_FLAGS=
FF_PLATFORM_CFG_FLAGS=

FF_EXTRA_CFLAGS=
FF_EXTRA_LDFLAGS="-Wl,-z,max-page-size=16384"



#--------------------
echo ""
echo "--------------------"
echo "[*] make NDK standalone toolchain"
echo "--------------------"
. ./tools/do-detect-env.sh
FF_MAKE_TOOLCHAIN_FLAGS=$IJK_MAKE_TOOLCHAIN_FLAGS
FF_MAKE_FLAGS=$IJK_MAKE_FLAG
FF_GCC_VER=$IJK_GCC_VER
FF_GCC_64_VER=$IJK_GCC_64_VER


#----- armv7a begin -----
if [ "$FF_ARCH" = "armv7a" ]; then
    FF_BUILD_NAME=openssl-armv7a
    FF_SOURCE=$FF_BUILD_ROOT/$FF_BUILD_NAME
	
    FF_CROSS_PREFIX=arm-linux-androideabi
	FF_TOOLCHAIN_NAME=${FF_CROSS_PREFIX}-${FF_GCC_VER}

    # OpenSSL 1.1.1w Configure target uses "android-arm" for 32-bit ARM.
    FF_PLATFORM_CFG_FLAGS="android-arm"

elif [ "$FF_ARCH" = "armv5" ]; then
    FF_BUILD_NAME=openssl-armv5
    FF_SOURCE=$FF_BUILD_ROOT/$FF_BUILD_NAME
	
    FF_CROSS_PREFIX=arm-linux-androideabi
	FF_TOOLCHAIN_NAME=${FF_CROSS_PREFIX}-${FF_GCC_VER}

    FF_PLATFORM_CFG_FLAGS="android"

elif [ "$FF_ARCH" = "x86" ]; then
    FF_BUILD_NAME=openssl-x86
    FF_SOURCE=$FF_BUILD_ROOT/$FF_BUILD_NAME
	
    FF_CROSS_PREFIX=i686-linux-android
	FF_TOOLCHAIN_NAME=x86-${FF_GCC_VER}

    FF_PLATFORM_CFG_FLAGS="android-x86"

    FF_CFG_FLAGS="$FF_CFG_FLAGS no-asm"

elif [ "$FF_ARCH" = "x86_64" ]; then
    FF_ANDROID_PLATFORM=android-21

    FF_BUILD_NAME=openssl-x86_64
    FF_SOURCE=$FF_BUILD_ROOT/$FF_BUILD_NAME

    FF_CROSS_PREFIX=x86_64-linux-android
    FF_TOOLCHAIN_NAME=${FF_CROSS_PREFIX}-${FF_GCC_64_VER}

    FF_PLATFORM_CFG_FLAGS="linux-x86_64"

elif [ "$FF_ARCH" = "arm64" ]; then
    FF_ANDROID_PLATFORM=android-21

    FF_BUILD_NAME=openssl-arm64
    FF_SOURCE=$FF_BUILD_ROOT/$FF_BUILD_NAME

    FF_CROSS_PREFIX=aarch64-linux-android
    FF_TOOLCHAIN_NAME=${FF_CROSS_PREFIX}-${FF_GCC_64_VER}

    FF_PLATFORM_CFG_FLAGS="linux-aarch64"

else
    echo "unknown architecture $FF_ARCH";
    exit 1
fi

FF_TOOLCHAIN_PATH=$FF_BUILD_ROOT/build/$FF_BUILD_NAME/toolchain

FF_SYSROOT=$FF_TOOLCHAIN_PATH/sysroot
FF_PREFIX=$FF_BUILD_ROOT/build/$FF_BUILD_NAME/output

mkdir -p $FF_PREFIX
# mkdir -p $FF_SYSROOT


#--------------------
echo ""
echo "--------------------"
echo "[*] make NDK standalone toolchain"
echo "--------------------"
. ./tools/do-detect-env.sh
FF_MAKE_TOOLCHAIN_FLAGS=$IJK_MAKE_TOOLCHAIN_FLAGS
FF_MAKE_FLAGS=$IJK_MAKE_FLAG


FF_MAKE_TOOLCHAIN_FLAGS="$FF_MAKE_TOOLCHAIN_FLAGS --install-dir=$FF_TOOLCHAIN_PATH"
FF_TOOLCHAIN_TOUCH="$FF_TOOLCHAIN_PATH/touch"
if [ ! -f "$FF_TOOLCHAIN_TOUCH" ]; then
    $ANDROID_NDK/build/tools/make-standalone-toolchain.sh \
        $FF_MAKE_TOOLCHAIN_FLAGS \
        --platform=$FF_ANDROID_PLATFORM \
        --toolchain=$FF_TOOLCHAIN_NAME
    touch $FF_TOOLCHAIN_TOUCH;
fi


#--------------------
echo ""
echo "--------------------"
echo "[*] check openssl env"
echo "--------------------"
export PATH=$FF_TOOLCHAIN_PATH/bin:$PATH

export COMMON_FF_CFG_FLAGS=

FF_CFG_FLAGS="$FF_CFG_FLAGS $COMMON_FF_CFG_FLAGS"

# NDK r22 ar wrappers may crash on recent macOS (Darwin arm64).
# Inject compatible wrappers that forward to llvm tools directly.
if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
    case "$IJK_NDK_REL" in
        22*)
            FF_COMPAT_BIN=$FF_TOOLCHAIN_PATH/compat-bin
            mkdir -p $FF_COMPAT_BIN
            cat > $FF_COMPAT_BIN/${FF_CROSS_PREFIX}-ar <<EOF
#! /usr/bin/env bash
exec "$FF_TOOLCHAIN_PATH/bin/llvm-ar" "\$@"
EOF
            cat > $FF_COMPAT_BIN/${FF_CROSS_PREFIX}-ranlib <<EOF
#! /usr/bin/env bash
exec "$FF_TOOLCHAIN_PATH/bin/llvm-ranlib" "\$@"
EOF
            if [ "$FF_ARCH" = "armv7a" ]; then
                cat > $FF_COMPAT_BIN/${FF_CROSS_PREFIX}-gcc <<EOF
#! /usr/bin/env bash
exec "$FF_TOOLCHAIN_PATH/bin/armv7a-linux-androideabi16-clang" "\$@"
EOF
                cat > $FF_COMPAT_BIN/${FF_CROSS_PREFIX}-g++ <<EOF
#! /usr/bin/env bash
exec "$FF_TOOLCHAIN_PATH/bin/armv7a-linux-androideabi16-clang++" "\$@"
EOF
                chmod +x $FF_COMPAT_BIN/${FF_CROSS_PREFIX}-gcc $FF_COMPAT_BIN/${FF_CROSS_PREFIX}-g++
            fi
            chmod +x $FF_COMPAT_BIN/${FF_CROSS_PREFIX}-ar $FF_COMPAT_BIN/${FF_CROSS_PREFIX}-ranlib
            export PATH=$FF_COMPAT_BIN:$PATH
            echo "Using ar/ranlib compatibility wrappers for Darwin arm64 + NDK r22"
        ;;
    esac
fi

#--------------------
# Standard options:
FF_CFG_FLAGS="$FF_CFG_FLAGS zlib-dynamic"
FF_CFG_FLAGS="$FF_CFG_FLAGS no-shared"
FF_CFG_FLAGS="$FF_CFG_FLAGS --openssldir=$FF_PREFIX"
FF_CFG_FLAGS="$FF_CFG_FLAGS --prefix=$FF_PREFIX"
if [ "$FF_ARCH" = "armv7a" ]; then
    export ANDROID_NDK_HOME=$FF_TOOLCHAIN_PATH
    export ANDROID_NDK=$FF_TOOLCHAIN_PATH
    export CC=gcc
    export CXX=g++
    FF_CFG_FLAGS="$FF_CFG_FLAGS -D__ANDROID_API__=16"
else
    FF_CFG_FLAGS="$FF_CFG_FLAGS --cross-compile-prefix=${FF_CROSS_PREFIX}-"
fi
FF_CFG_FLAGS="$FF_CFG_FLAGS $FF_PLATFORM_CFG_FLAGS"

#--------------------
echo ""
echo "--------------------"
echo "[*] configurate openssl"
echo "--------------------"
cd $FF_SOURCE
#if [ -f "./Makefile" ]; then
#    echo 'reuse configure'
#else
    echo "./Configure $FF_CFG_FLAGS"
    ./Configure $FF_CFG_FLAGS
#        --extra-cflags="$FF_CFLAGS $FF_EXTRA_CFLAGS" \
#        --extra-ldflags="$FF_EXTRA_LDFLAGS"
#fi

#--------------------
echo ""
echo "--------------------"
echo "[*] compile openssl"
echo "--------------------"
make depend
make $FF_MAKE_FLAGS
make install_sw

#--------------------
echo ""
echo "--------------------"
echo "[*] link openssl"
echo "--------------------"
