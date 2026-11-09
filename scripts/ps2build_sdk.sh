#!/bin/sh
# Install the pinned ps2build SDK into DEST from the official GitHub releases (github.com/ps2build) or, when
# they fail, from our private mirror.
#
# Usage: ps2build_sdk.sh [--llvm] DEST
#        ps2build_sdk.sh --resolve [--llvm] OUT    (CI, before the cache step: write the versions to OUT)
#
# The pin file (PS2BUILD_SDK_PIN, default .github/ps2build-sdk.env) lists the components as name=vX, so
# resolving them asks no server anything. .github/workflows/ps2build-sdk-watch.yml is what asks the
# official release API, once a week, and proposes new pins. A bare name (or "latest") still works and
# follows the newest official release (from ps2build's manifest, else the release API) in every run.
# The versions are installed from:
#   1. DEST, when it already holds exactly those versions (an actions/cache restore).
#   2. The official GitHub releases, "ps2build update <component> --version <tag>" for each component that differs,
#      so an older cached SDK only downloads what changed. The whole attempt gets
#      PS2BUILD_SDK_OFFICIAL_BUDGET seconds (default 900), each component one retry.
#   3. A NathanNeurotic/old-ps2build-mirror release, when the official install fails: the one the pin file
#      names (PS2BUILD_SDK_TAG, with the SHA-256 of its SHA256SUMS), else the newest. Every file is
#      checked against that release's SHA256SUMS. When the newest release does not hold the wanted
#      versions, the job says so and sets PS2BUILD_SDK_STALE=true, so CI does not cache it under them.
# When a bare name needs the release API and it is unreachable, the newest mirror release decides.
# PS2BUILD_SDK_NO_FALLBACK=1 fails instead of using the mirror. On GitHub Actions DEST is added to PATH
# and PS2BUILD_SDK_SOURCE (cache, official or mirror) to the environment.
set -eu

usage() {
    echo "usage: $0 [--llvm] DEST | $0 --resolve [--llvm] OUT" >&2
    exit 2
}

MODE=install
LLVM=0
while [ $# -gt 0 ]; do
    case $1 in
        --resolve) MODE=resolve ;;
        --official-only) MODE=official ;; # internal: the official attempt, run under one timeout
        --llvm) LLVM=1 ;;
        -*) usage ;;
        *) break ;;
    esac
    shift
done
[ $# -eq 1 ] || usage
DEST=$1
PIN=${PS2BUILD_SDK_PIN:-.github/ps2build-sdk.env}
OFFICIAL=${PS2BUILD_SDK_OFFICIAL_URL:-https://github.com/ps2build}
OFFICIAL_API=${PS2BUILD_SDK_OFFICIAL_API_URL:-https://api.github.com/repos/ps2build}
MANIFEST_URL=${PS2BUILD_SDK_MANIFEST_URL:-https://raw.githubusercontent.com/ps2build/manifest/main/latest.json}
MANIFEST_FILE=
STALE=0

[ -f "$PIN" ] || { echo "error: pin file $PIN not found" >&2; exit 2; }
PS2BUILD_SDK_MIRROR= PS2BUILD_SDK_TAG= PS2BUILD_SDK_SUMS_SHA256= PS2BUILD_COMPONENTS= PS2BUILD_LLVM_TOOLCHAIN=
PS2BUILD_EXTRAS= PS2BUILD_EXTRAS_TAG= PS2BUILD_EXTRAS_SUMS_SHA256= PS2BUILD_SDK_RESOLVED=
# shellcheck disable=SC1090
case $PIN in /*) . "$PIN" ;; *) . "./$PIN" ;; esac
[ -n "$PS2BUILD_COMPONENTS" ] || { echo "error: $PIN sets no PS2BUILD_COMPONENTS" >&2; exit 2; }
PS2BUILD_SDK_MIRROR=${PS2BUILD_SDK_MIRROR:-NathanNeurotic/old-ps2build-mirror}
PS2BUILD_LLVM_TOOLCHAIN=${PS2BUILD_LLVM_TOOLCHAIN:-latest}
MIRROR_LATEST=${PS2BUILD_SDK_MIRROR_LATEST_URL:-https://github.com/$PS2BUILD_SDK_MIRROR/releases/latest/download}
# The mirror may be a private repository. Its download links then need a login, so with
# PS2BUILD_SDK_MIRROR_TOKEN set (a token that can read the mirror's contents, from a CI secret) every
# mirror file comes through the GitHub API instead. Without it the public download links are used.
MIRROR_TOKEN=${PS2BUILD_SDK_MIRROR_TOKEN:-}
unset PS2BUILD_SDK_MIRROR_TOKEN # keep it out of the environment of ps2build, curl and the official attempt
MIRROR_API=${PS2BUILD_SDK_MIRROR_API_URL:-https://api.github.com}

log() { echo "ps2build-sdk: $*"; }

# Sent with every request, so upstream can tell ours apart.
USER_AGENT="Launchstation-build/1.0 (+https://github.com/NathanNeurotic/Launchstation)"

warn() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::warning::$*"; else log "warning: $*"; fi
}

differences() { # "name=version ..." of the mirror and of the wanted SDK -> "name mirror (wanted X), ..."
    for pair in $2; do
        mirror=$(version_in "${pair%%=*}" "$1" || echo none)
        [ "$mirror" = "${pair#*=}" ] || printf '%s %s (wanted %s) ' "${pair%%=*}" "$mirror" "${pair#*=}"
    done
}

fetch() { # url file
    # --speed-limit: a stalled transfer is aborted (and retried) instead of hanging the job.
    curl -sSfL -A "$USER_AGENT" --retry 6 --retry-delay 5 --retry-all-errors --connect-timeout 20 --speed-limit 1024 --speed-time 60 "$1" -o "$2"
}

sha256() { sha256sum "$1" | cut -d' ' -f1; }

api_fetch() { # url file accept: a GitHub API request with the mirror token, which stays off the command line
    # curl sends the Authorization header to api.github.com only, not to the storage host it redirects to.
    # HTTPS only, redirects included, so the token never travels in cleartext.
    printf 'header = "Authorization: Bearer %s"\n' "$MIRROR_TOKEN" |
        curl -K - -sSfL -A "$USER_AGENT" --proto '=https' --proto-redir '=https' --retry 6 --retry-delay 5 --retry-all-errors --connect-timeout 20 --speed-limit 1024 --speed-time 60 \
            -H "Accept: $3" -H "X-GitHub-Api-Version: 2022-11-28" "$1" -o "$2"
}

asset_id() { # name < release JSON -> the id of the one asset with that name
    if command -v jq >/dev/null 2>&1; then
        jq -r --arg name "$1" '[.assets[]? | select(.name == $name) | .id] | if length == 1 then .[0] else empty end'
    else
        python3 -I -c 'import json, sys
ids = [a["id"] for a in json.load(sys.stdin).get("assets", []) if a.get("name") == sys.argv[1]]
print(ids[0] if len(ids) == 1 else "")' "$1"
    fi | grep -E '^[0-9]+$'
}

mirror_fetch() { # release (latest or a tag) name file: one file of a ps2build-mirror release
    if [ -z "$MIRROR_TOKEN" ]; then
        if [ "$1" = latest ]; then
            fetch "$MIRROR_LATEST/$2" "$3"
        else
            fetch "${PS2BUILD_SDK_MIRROR_URL:-https://github.com/$PS2BUILD_SDK_MIRROR/releases/download/$1}/$2" "$3"
        fi
        return
    fi
    case $1 in latest) release=latest ;; *) release=tags/$1 ;; esac
    api_fetch "$MIRROR_API/repos/$PS2BUILD_SDK_MIRROR/releases/$release" "$3.release" application/vnd.github+json || {
        rm -f "$3.release"
        log "cannot read ps2build-mirror release $1 (is PS2BUILD_SDK_MIRROR_TOKEN allowed to read $PS2BUILD_SDK_MIRROR?)"
        return 1
    }
    id=$(asset_id "$2" < "$3.release") || id=
    rm -f "$3.release"
    [ -n "$id" ] || { log "ps2build-mirror release $1 has no single asset named $2"; return 1; }
    api_fetch "$MIRROR_API/repos/$PS2BUILD_SDK_MIRROR/releases/assets/$id" "$3" application/octet-stream
}

# Versions, tags and hashes from the network end up in a pin file that later steps source, and some are
# split into words and evaluated, so anything but these characters is refused (an empty result, as if the
# value were missing). Only PS2BUILD_COMPONENTS ("name=version ...") may hold spaces and "=".
pin_value() { # name file -> the value a pin.env assigns, without quotes
    case $1 in PS2BUILD_COMPONENTS) chars='A-Za-z0-9._+= -' ;; *) chars='A-Za-z0-9._+-' ;; esac
    sed -n "s/^$1=\"\{0,1\}\([$chars]*\)\"\{0,1\}\$/\1/p" "$2" | head -n 1
}

# A version taken from a mirror pin.env must be there: an empty one would be read as "latest" later.
mirror_value() { # name file -> the value, or fail when it is missing or refused
    value=$(pin_value "$1" "$2")
    [ -n "$value" ] || { log "the newest ps2build-mirror pin.env has no valid $1" >&2; return 1; }
    echo "$value"
}

version_in() { # component "name=version ..." -> version
    for pair in $2; do
        [ "${pair%%=*}" = "$1" ] && { echo "${pair#*=}"; return 0; }
    done
    return 1
}

version_of() { version_in "$1" "$PS2BUILD_COMPONENTS"; }

extra_var() { echo "$1" | tr 'a-z-' 'A-Z_'; } # ogg -> OGG, read as OGG_VERSION

latest_tag() { # repository -> its newest official release tag; ps2build's manifest first
    if [ -n "$MANIFEST_FILE" ] && [ -s "$MANIFEST_FILE" ]; then
        tag=$(python3 -I -c 'import json,sys,re
tag=json.load(open(sys.argv[2])).get("components",{}).get(sys.argv[1],{}).get("tag","")
if not re.fullmatch(r"[A-Za-z0-9._+-]+",tag): sys.exit(1)
print(tag)' "$1" "$MANIFEST_FILE") && { printf '%s\n' "$tag"; return 0; }
    fi
    curl -sSfL -A "$USER_AGENT" --retry 3 --retry-delay 5 --retry-all-errors --connect-timeout 20 --max-time 60 \
        "$OFFICIAL_API/$1/releases/latest" | python3 -I -c 'import json,sys,re
tag=json.load(sys.stdin).get("tag_name","")
if not re.fullmatch(r"[A-Za-z0-9._+-]+",tag): sys.exit(1)
print(tag)'
}

resolve_official() { # fill in each unpinned version with the newest official release
    lookup=0 # a fully pinned file asks nobody anything
    for entry in $PS2BUILD_COMPONENTS; do case $entry in *=*) ;; *) lookup=1 ;; esac; done
    [ "$PS2BUILD_LLVM_TOOLCHAIN" = latest ] && lookup=1
    for name in $PS2BUILD_EXTRAS; do eval "[ \"\${$(extra_var "$name")_VERSION:-latest}\" = latest ]" && lookup=1; done
    if [ "$lookup" = 1 ]; then
        MANIFEST_FILE=$(mktemp)
        fetch "$MANIFEST_URL" "$MANIFEST_FILE" || { rm -f "$MANIFEST_FILE"; MANIFEST_FILE=; warn "cannot fetch ps2build's release manifest; asking the release API instead"; }
    fi
    resolved=
    for entry in $PS2BUILD_COMPONENTS; do
        case $entry in *=*) ;; *) entry="$entry=$(latest_tag "$entry")" || return 1 ;; esac
        resolved="$resolved${resolved:+ }$entry"
    done
    if [ "$PS2BUILD_LLVM_TOOLCHAIN" = latest ]; then
        llvm_version=$(latest_tag llvm-toolchain) || return 1
    else
        llvm_version=$PS2BUILD_LLVM_TOOLCHAIN
    fi
    extras=
    for name in $PS2BUILD_EXTRAS; do
        eval "version=\${$(extra_var "$name")_VERSION:-latest}"
        [ "$version" = latest ] && { version=$(latest_tag "$name") || return 1; }
        extras="$extras $(extra_var "$name")_VERSION=$version"
    done
    PS2BUILD_COMPONENTS=$resolved
    PS2BUILD_LLVM_TOOLCHAIN=$llvm_version
    for assignment in $extras; do eval "$assignment"; done
    [ -z "$MANIFEST_FILE" ] || rm -f "$MANIFEST_FILE"
    MANIFEST_FILE=
}

resolve_mirror() { # the release API is down: the newest mirror release defines the unpinned versions
    pin_tmp=$(mktemp)
    if ! mirror_fetch latest pin.env "$pin_tmp"; then
        rm -f "$pin_tmp"
        return 1
    fi
    mirror_components=$(pin_value PS2BUILD_COMPONENTS "$pin_tmp")
    resolved=
    for entry in $PS2BUILD_COMPONENTS; do
        case $entry in *=*) ;; *) entry="$entry=$(version_in "$entry" "$mirror_components")" || { rm -f "$pin_tmp"; return 1; } ;; esac
        resolved="$resolved${resolved:+ }$entry"
    done
    PS2BUILD_COMPONENTS=$resolved
    if [ "$PS2BUILD_LLVM_TOOLCHAIN" = latest ]; then
        PS2BUILD_LLVM_TOOLCHAIN=$(mirror_value PS2BUILD_LLVM_TOOLCHAIN "$pin_tmp") || { rm -f "$pin_tmp"; return 1; }
    fi
    for name in $PS2BUILD_EXTRAS; do
        var=$(extra_var "$name")_VERSION
        eval "version=\${$var:-latest}"
        [ "$version" = latest ] || continue
        if [ -n "$PS2BUILD_EXTRAS_TAG" ]; then # the SDK mirror cannot say what the separate extras release holds
            log "$var is unpinned, and the extras come from $PS2BUILD_EXTRAS_TAG; pin it"
            rm -f "$pin_tmp"
            return 1
        fi
        eval "$var=\$(mirror_value $var \"\$pin_tmp\")" || { rm -f "$pin_tmp"; return 1; }
    done
    PS2BUILD_SDK_TAG=$(pin_value PS2BUILD_SDK_TAG "$pin_tmp")
    PS2BUILD_SDK_SUMS_SHA256=$(pin_value PS2BUILD_SDK_SUMS_SHA256 "$pin_tmp")
    rm -f "$pin_tmp"
    [ -n "$PS2BUILD_SDK_TAG" ] && [ -n "$PS2BUILD_SDK_SUMS_SHA256" ] || { log "the newest ps2build-mirror pin.env is incomplete"; return 1; }
    warn "$OFFICIAL's release API is unreachable; using the newest ps2build-mirror release $PS2BUILD_SDK_TAG, which may be older than the latest SDK"
}

write_pin() { # the resolved versions, in the pin format later steps read
    {
        echo "# Resolved by ps2build_sdk.sh from $PIN"
        echo "PS2BUILD_SDK_RESOLVED=1"
        echo "PS2BUILD_SDK_MIRROR=$PS2BUILD_SDK_MIRROR"
        echo "PS2BUILD_SDK_TAG=$PS2BUILD_SDK_TAG"
        echo "PS2BUILD_SDK_SUMS_SHA256=$PS2BUILD_SDK_SUMS_SHA256"
        echo "PS2BUILD_COMPONENTS=\"$PS2BUILD_COMPONENTS\""
        echo "PS2BUILD_LLVM_TOOLCHAIN=$PS2BUILD_LLVM_TOOLCHAIN"
        echo "PS2BUILD_EXTRAS=\"$PS2BUILD_EXTRAS\""
        echo "PS2BUILD_EXTRAS_TAG=$PS2BUILD_EXTRAS_TAG"
        echo "PS2BUILD_EXTRAS_SUMS_SHA256=$PS2BUILD_EXTRAS_SUMS_SHA256"
        for name in $PS2BUILD_EXTRAS; do
            var=$(extra_var "$name")_VERSION
            eval "echo \"$var=\${$var}\""
        done
    } > "$1"
}

if [ "$PS2BUILD_SDK_RESOLVED" != 1 ]; then
    if resolve_official; then
        :
    elif [ "${PS2BUILD_SDK_NO_FALLBACK:-0}" = 1 ]; then
        echo "error: $OFFICIAL's release API is unreachable and PS2BUILD_SDK_NO_FALLBACK=1" >&2
        exit 1
    else
        resolve_mirror || { echo "error: neither $OFFICIAL nor the mirror's newest release is reachable" >&2; exit 1; }
    fi
fi
[ "$LLVM" = 0 ] || [ -n "$PS2BUILD_LLVM_TOOLCHAIN" ] || { echo "error: no llvm-toolchain version" >&2; exit 1; }

if [ "$MODE" = resolve ]; then
    write_pin "$DEST"
    # The key covers what lands in DEST: the components, llvm-toolchain for --llvm, and the extra packages.
    key=$(grep -v '^PS2BUILD_SDK_\(TAG\|SUMS_SHA256\)=' "$DEST" | { if [ "$LLVM" = 1 ]; then cat; else grep -v '^PS2BUILD_LLVM_TOOLCHAIN='; fi; } \
        | sha256sum | cut -c1-16)
    log "versions: $PS2BUILD_COMPONENTS$([ "$LLVM" = 1 ] && echo " llvm-toolchain=$PS2BUILD_LLVM_TOOLCHAIN")"
    if [ -n "${GITHUB_ENV:-}" ]; then
        echo "PS2BUILD_SDK_PIN=$DEST" >> "$GITHUB_ENV"
    fi
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        echo "key=$key" >> "$GITHUB_OUTPUT"
    fi
    echo "key=$key"
    exit 0
fi

wanted() { # "component version" lines this job needs
    for pair in $PS2BUILD_COMPONENTS; do
        echo "${pair%%=*} ${pair#*=}"
    done
    [ "$LLVM" = 1 ] && echo "llvm-toolchain $PS2BUILD_LLVM_TOOLCHAIN"
    return 0
}

has_version() { # component version: DEST's versions.yaml records it
    grep -Eq "^ +$1: $2\$" "$DEST/versions.yaml" 2>/dev/null
}

installed_ok() { # DEST holds every wanted component at its resolved version
    [ -x "$DEST/ps2build" ] && [ -f "$DEST/versions.yaml" ] || return 1
    wanted | while read -r name version; do
        if [ "$name" = ps2build ]; then
            "$DEST/ps2build" --version 2>/dev/null | grep -qx "ps2build $version" || { echo missing; break; }
        else
            has_version "$name" "$version" || { echo missing; break; }
        fi
    done | grep -q missing && return 1
    [ "$LLVM" = 0 ] || [ -f "$DEST/llvm-toolchain/ee/bin/mips64r5900el-ps2-elf-clang.cfg" ] || return 1
    return 0
}

reset_dest() { # never delete something that is not an SDK install
    if [ -e "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
        # ps2build may be a partial download from an interrupted official attempt, so not -x.
        if [ ! -f "$DEST/versions.yaml" ] && [ ! -f "$DEST/ps2build" ] && [ ! -f "$DEST/ps2build.new" ]; then
            echo "error: $DEST is not empty and is not a ps2build SDK; refusing to replace it" >&2
            exit 1
        fi
        log "replacing the SDK in $DEST"
        rm -rf "$DEST"
    fi
    mkdir -p "$DEST"
}

adopt_mirror_pin() { # pin.env of the newest mirror release: install that, and say so when it differs
    tag=$(pin_value PS2BUILD_SDK_TAG "$1")
    sums=$(pin_value PS2BUILD_SDK_SUMS_SHA256 "$1")
    components=$(pin_value PS2BUILD_COMPONENTS "$1")
    llvm=$(pin_value PS2BUILD_LLVM_TOOLCHAIN "$1")
    [ -n "$tag" ] && [ -n "$sums" ] && [ -n "$components" ] && { [ "$LLVM" = 0 ] || [ -n "$llvm" ]; } \
        || { log "the newest ps2build-mirror pin.env is incomplete"; return 1; }
    [ "$LLVM" = 1 ] && components_llvm="$components llvm-toolchain=$llvm" latest_llvm="$PS2BUILD_COMPONENTS llvm-toolchain=$PS2BUILD_LLVM_TOOLCHAIN" \
        || components_llvm=$components latest_llvm=$PS2BUILD_COMPONENTS
    older=$(differences "$components_llvm" "$latest_llvm")
    if [ -n "$older" ]; then
        STALE=1
        warn "the newest ps2build-mirror release $tag does not hold the wanted SDK: ${older% }"
    fi
    PS2BUILD_SDK_TAG=$tag PS2BUILD_SDK_SUMS_SHA256=$sums PS2BUILD_COMPONENTS=$components PS2BUILD_LLVM_TOOLCHAIN=$llvm
    if [ -z "$PS2BUILD_EXTRAS_TAG" ]; then # with a separate extras release, the extras keep their own pins
        for name in $PS2BUILD_EXTRAS; do
            var=$(extra_var "$name")_VERSION
            eval "$var=\$(mirror_value $var \"\$1\")" || return 1
        done
    fi
    # Later steps (the ogg/vorbis packages) then take the same release.
    if [ "$PS2BUILD_SDK_RESOLVED" = 1 ]; then
        write_pin "$PIN"
    fi
}

from_mirror() {
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    if [ -z "$PS2BUILD_SDK_TAG" ]; then
        mirror_fetch latest pin.env "$work/pin.env" || { log "cannot download the newest ps2build-mirror pin.env"; return 1; }
        adopt_mirror_pin "$work/pin.env" || return 1
    fi
    mirror_fetch "$PS2BUILD_SDK_TAG" SHA256SUMS "$work/SHA256SUMS" || { log "cannot download SHA256SUMS of $PS2BUILD_SDK_TAG"; return 1; }
    got=$(sha256 "$work/SHA256SUMS")
    if [ "$got" != "$PS2BUILD_SDK_SUMS_SHA256" ]; then
        echo "error: mirror SHA256SUMS is $got, its pin.env says $PS2BUILD_SDK_SUMS_SHA256" >&2
        return 1
    fi
    parts=base
    [ "$LLVM" = 1 ] && parts="base llvm"
    reset_dest
    for part in $parts; do
        name=$(sed -n "s/^[0-9a-f]\{64\}  \(ps2build-sdk-.*-linux-amd64-$part\.tar\.zst\)\$/\1/p" "$work/SHA256SUMS")
        [ -n "$name" ] || { echo "error: SHA256SUMS lists no $part archive" >&2; return 1; }
        log "downloading $name"
        mirror_fetch "$PS2BUILD_SDK_TAG" "$name" "$work/$name" || { log "cannot download $name"; return 1; }
        (cd "$work" && grep "  $name\$" SHA256SUMS | sha256sum -c --quiet -) || {
            echo "error: $name does not match SHA256SUMS" >&2
            return 1
        }
        tar --zstd -xf "$work/$name" -C "$DEST"
        rm -f "$work/$name"
    done
    installed_ok || { echo "error: the mirror archives do not hold the versions their pin.env names" >&2; return 1; }
}

update_component() { # name version, one retry: the mirror covers a longer outage
    for wait in 20 0; do
        # ps2build has no download timeout of its own; a stalled transfer would hang the job.
        # --foreground keeps it in our process group, so the overall budget can stop it too.
        timeout --foreground "${PS2BUILD_SDK_UPDATE_TIMEOUT:-600}" "$DEST/ps2build" update "$1" --version "$2" && return 0
        [ "$wait" = 0 ] && break
        log "update $1 $2 failed; retrying in $wait s"
        sleep "$wait"
    done
    return 1
}

from_official() {
    # An older SDK (a cache restore of an earlier release) is updated in place: only what changed is fetched.
    if [ ! -f "$DEST/versions.yaml" ] || [ ! -x "$DEST/ps2build" ]; then
        reset_dest
    fi
    version=$(version_of ps2build)
    if ! "$DEST/ps2build" --version 2>/dev/null | grep -qx "ps2build $version"; then
        log "downloading ps2build $version from $OFFICIAL"
        fetch "$OFFICIAL/ps2build/releases/download/$version/ps2build-$version-linux-amd64" "$DEST/ps2build.new" || return 1
        chmod +x "$DEST/ps2build.new"
        mv -f "$DEST/ps2build.new" "$DEST/ps2build"
    fi
    wanted | while read -r name version; do
        [ "$name" = ps2build ] && continue
        has_version "$name" "$version" && continue
        update_component "$name" "$version" || exit 1
    done || return 1
    installed_ok || { echo "error: the official install does not hold the resolved versions" >&2; return 1; }
}

if [ "$MODE" = official ]; then
    echo $$ > "$PS2BUILD_SDK_PGID_FILE"
    from_official
    exit
fi

official() { # the official attempt in its own process group, so a timeout also stops ps2build and curl
    PS2BUILD_SDK_PGID_FILE=$(mktemp)
    export PS2BUILD_SDK_PGID_FILE
    resolved_pin=$(mktemp)
    write_pin "$resolved_pin"
    llvm_flag=
    [ "$LLVM" = 1 ] && llvm_flag=--llvm
    status=0
    PS2BUILD_SDK_PIN=$resolved_pin timeout "${PS2BUILD_SDK_OFFICIAL_BUDGET:-900}" \
        setsid sh "$0" --official-only $llvm_flag "$DEST" || status=$?
    if [ "$status" != 0 ]; then
        [ "$status" = 124 ] && log "the official install took longer than ${PS2BUILD_SDK_OFFICIAL_BUDGET:-900} s"
        pgid=$(cat "$PS2BUILD_SDK_PGID_FILE" 2>/dev/null || true)
        # Nothing left over may keep writing into DEST while the mirror replaces it.
        [ -n "$pgid" ] && env kill -KILL -- "-$pgid" 2>/dev/null || true  # dash's kill takes no process group
    fi
    rm -f "$PS2BUILD_SDK_PGID_FILE" "$resolved_pin"
    return "$status"
}

if installed_ok; then
    source=cache
elif official; then
    source=official
elif [ "${PS2BUILD_SDK_NO_FALLBACK:-0}" = 1 ]; then
    echo "error: the official install failed and PS2BUILD_SDK_NO_FALLBACK=1" >&2
    exit 1
else
    warn "$OFFICIAL failed; installing the ps2build SDK from the ps2build-mirror release"
    from_mirror || { echo "error: both $OFFICIAL and the mirror failed" >&2; exit 1; }
    source=mirror
fi
rm -rf "$DEST/.update-staging" "$DEST/ps2build.new"

log "SDK ready in $DEST (from $source)"
if [ -n "${GITHUB_ENV:-}" ]; then
    echo "$DEST" >> "$GITHUB_PATH"
    echo "PS2BUILD_SDK_SOURCE=$source" >> "$GITHUB_ENV"
    [ "$STALE" = 1 ] && echo "PS2BUILD_SDK_STALE=true" >> "$GITHUB_ENV"
fi
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    echo "ps2build SDK from **$source**: \`$PS2BUILD_COMPONENTS\`$([ "$STALE" = 1 ] && echo " (not the wanted versions)")" >> "$GITHUB_STEP_SUMMARY"
fi
