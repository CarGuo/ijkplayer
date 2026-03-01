#! /usr/bin/env bash
#
# Copyright (C) 2013-2015 Bilibili
# Copyright (C) 2013-2015 Zhang Rui <bbcallen@gmail.com>
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

# IJK_FFMPEG_UPSTREAM=git://git.videolan.org/ffmpeg.git
# IJK_FFMPEG_UPSTREAM=https://github.com/CarGuo/FFmpeg.git
# IJK_FFMPEG_FORK=https://github.com/CarGuo/FFmpeg.git
IJK_FFMPEG_UPSTREAM=https://github.com/CarGuo/FFmpeg.git
IJK_FFMPEG_FORK=https://github.com/CarGuo/FFmpeg.git
IJK_FFMPEG_COMMIT=ijk-n4.3-20260301-007
IJK_FFMPEG_LOCAL_REPO=extra/ffmpeg

set -e
TOOLS=tools

git --version

echo "== pull ffmpeg base =="
sh $TOOLS/pull-repo-base.sh $IJK_FFMPEG_UPSTREAM $IJK_FFMPEG_LOCAL_REPO

function ensure_ffmpeg_openssl_probe()
{
    local ffmpeg_dir=$1
    local cfg_file=$ffmpeg_dir/configure

    if [ ! -f "$cfg_file" ]; then
        echo "!! WARNING: missing configure in $ffmpeg_dir"
        return 0
    fi

    if grep -q 'check_lib openssl openssl/ssl.h OPENSSL_init_ssl -lssl -lcrypto' "$cfg_file"; then
        echo "openssl OPENSSL_init_ssl check already exists: $cfg_file"
        return 0
    fi

    echo "patching openssl OPENSSL_init_ssl check: $cfg_file"
    awk '
        {
            print
            if (!done && $0 ~ /check_pkg_config openssl openssl openssl\/ssl.h OPENSSL_init_ssl \|\|/) {
                print "                               check_lib openssl openssl/ssl.h OPENSSL_init_ssl -lssl -lcrypto ||"
                done=1
            }
        }
        END {
            if (!done) exit 2
        }
    ' "$cfg_file" > "$cfg_file.tmp"
    mv "$cfg_file.tmp" "$cfg_file"
    chmod +x "$cfg_file"
}

function pull_fork()
{
    echo "== pull ffmpeg fork $1 =="
    sh $TOOLS/pull-repo-ref.sh $IJK_FFMPEG_FORK android/contrib/ffmpeg-$1 ${IJK_FFMPEG_LOCAL_REPO}
    cd android/contrib/ffmpeg-$1
    git checkout ${IJK_FFMPEG_COMMIT} -B ijkplayer
    cd -
    ensure_ffmpeg_openssl_probe "android/contrib/ffmpeg-$1"
}

pull_fork "armv5"
pull_fork "armv7a"
pull_fork "arm64"
pull_fork "x86"
pull_fork "x86_64"

./init-config.sh
./init-android-libyuv.sh
./init-android-soundtouch.sh
