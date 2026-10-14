// crates/shell/src/host/bundle/*.sh at 7a209417. The bytes are the
// templates bundle fills. A placeholder is @NAME@; the scripts
// themselves are not executed here.
#include "base.h"

namespace gpui::shell {

extern const char kBundleRuntimeSh[] =
    R"BUNDLESCRIPT(# Finds gpui-shell, or installs the release this bundle was made with.
# Included by the launchers and the installer; the includer defines
# `runtime_notice <message>` and `runtime_fail <message>` (which must not return).

runtime_version=@RUNTIME_VERSION@
runtime_repository=longbridge/gpui-kit

# Whether version $1 is at least $2. Numeric X.Y.Z; a pre-release suffix on an
# otherwise equal version is lower, as semver orders it.
runtime_at_least() {
  awk -v have="$1" -v want="$2" 'BEGIN {
    split(have, h, "-"); split(want, w, "-")
    n = split(h[1], a, "."); m = split(w[1], b, ".")
    if (n != 3 || m != 3) exit 1
    for (i = 1; i <= 3; i++) {
      if (a[i] !~ /^[0-9]+$/) exit 1
      if (a[i] + 0 > b[i] + 0) exit 0
      if (a[i] + 0 < b[i] + 0) exit 1
    }
    # Equal numbers: a release beats any pre-release of itself.
    exit (h[2] != "" && w[2] == "") ? 1 : 0
  }'
}

# Whether $1 is a gpui-shell this bundle can run on.
runtime_suits() {
  [ -x "$1" ] || return 1
  candidate_version=$("$1" --version 2>/dev/null | awk '$1 == "gpui-shell" { print $2; exit }') || return 1
  [ -n "$candidate_version" ] && runtime_at_least "$candidate_version" "$runtime_version"
}

# Sets `runtime` to a suitable gpui-shell, downloading one into ~/.local/bin
# when none is installed. ~/.local/bin is where the official installer puts it.
ensure_runtime() {
  for candidate in ~/.local/bin/gpui-shell "$(command -v gpui-shell 2>/dev/null || true)" \
    /opt/homebrew/bin/gpui-shell /usr/local/bin/gpui-shell ~/.cargo/bin/gpui-shell; do
    if [ -n "$candidate" ] && runtime_suits "$candidate"; then
      runtime=$candidate
      return 0
    fi
  done
  runtime_install
}

runtime_install() {
  case "$(uname -s)" in
    Darwin) runtime_os=apple-darwin ;;
    Linux) runtime_os=unknown-linux-gnu ;;
    *) runtime_fail "gpui-shell has no release for $(uname -s)." ;;
  esac
  case "$(uname -m)" in
    arm64 | aarch64) runtime_arch=aarch64 ;;
    x86_64 | amd64) runtime_arch=x86_64 ;;
    *) runtime_fail "gpui-shell has no release for $(uname -m)." ;;
  esac
  runtime_target="$runtime_arch-$runtime_os"
  if [ "$runtime_target" = aarch64-unknown-linux-gnu ]; then
    runtime_fail "gpui-shell has no release for Linux on ARM yet; build it with \`cargo build -p gpui-shell --release\` and put it in ~/.local/bin."
  fi
  for utility in curl tar mktemp; do
    command -v "$utility" >/dev/null 2>&1 || runtime_fail "Installing gpui-shell needs $utility."
  done

  runtime_asset="gpui-shell-$runtime_target.tar.gz"
  runtime_url="https://github.com/$runtime_repository/releases/download/v$runtime_version/$runtime_asset"
  runtime_bin=~/.local/bin
  mkdir -p "$runtime_bin" || runtime_fail "Cannot create $runtime_bin."
  runtime_work=$(mktemp -d) || runtime_fail 'Cannot create a temporary directory.'
  runtime_notice "Downloading gpui-shell $runtime_version…"
  if ! runtime_fetch; then
    rm -rf "$runtime_work"
    runtime_fail "Could not install gpui-shell $runtime_version from $runtime_url."
  fi
  rm -rf "$runtime_work"
  runtime="$runtime_bin/gpui-shell"
  runtime_notice "Installed gpui-shell $runtime_version in $runtime_bin."
}

# Downloads, verifies and installs into $runtime_bin. Fails without touching
# an installed copy: the new binary replaces it only after it checks out.
# Every step is checked explicitly: `set -e` does not apply inside a function
# whose status is tested.
runtime_fetch() (
  cd "$runtime_work" || exit 1
  curl --fail --silent --show-error --location --retry 3 "$runtime_url" -o "$runtime_asset" || exit 1
  curl --fail --silent --show-error --location --retry 3 "$runtime_url.sha256" -o "$runtime_asset.sha256" || exit 1
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 -c "$runtime_asset.sha256" >/dev/null || exit 1
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum -c "$runtime_asset.sha256" >/dev/null || exit 1
  else
    echo 'Missing a SHA-256 verifier (shasum or sha256sum).' >&2
    exit 1
  fi
  tar -xzf "$runtime_asset" || exit 1
  staged="$runtime_bin/.gpui-shell.$$"
  if ! cp "gpui-shell-$runtime_target/gpui-shell" "$staged" || ! chmod 755 "$staged" ||
    ! runtime_suits "$staged"; then
    rm -f "$staged"
    echo "The downloaded gpui-shell is not version $runtime_version or later." >&2
    exit 1
  fi
  mv -f "$staged" "$runtime_bin/gpui-shell" || { rm -f "$staged"; exit 1; }
)
)BUNDLESCRIPT";

extern const char kBundleLinuxLauncherSh[] =
    R"BUNDLESCRIPT(#!/bin/sh
# Starts @NAME@ with an installed gpui-shell. Written by `gpui-shell bundle`.
set -eu

[ "$#" -eq 0 ] || { echo 'This launcher takes no arguments.' >&2; exit 2; }

# A desktop launch has no terminal, so tell the user through a notification.
runtime_notice() {
  if [ -t 2 ]; then
    echo "$1" >&2
  elif command -v notify-send >/dev/null 2>&1; then
    notify-send -- @NAME@ "$1" 2>/dev/null || true
  fi
}

runtime_fail() {
  if [ -t 2 ]; then
    echo "$1" >&2
  elif command -v notify-send >/dev/null 2>&1; then
    notify-send --urgency=critical -- @NAME@ "$1" 2>/dev/null || true
  fi
  exit 1
}

@RUNTIME_FUNCTIONS@

# Resolved through the ~/.local/bin link to the installed directory.
dir=$(dirname "$(readlink -f "$0")")
ensure_runtime
exec "$runtime" run --dir "$dir/app" --packaged
)BUNDLESCRIPT";

extern const char kBundleLinuxInstallSh[] =
    R"BUNDLESCRIPT(#!/bin/sh
# Installs @NAME@ for the current user. Written by `gpui-shell bundle`.
#
#   sh install.sh                   install from @ARCHIVE@ beside this script,
#                                   or download it; also updates an installed copy
#   sh install.sh <archive or URL>  install from that archive
#   sh install.sh --uninstall       remove it; its saved data and gpui-shell stay
#   curl -fsSL <url>/install.sh | sh
#
# No sudo: everything goes under ~/.local. The application runs on the
# gpui-shell in ~/.local/bin, which is installed when no suitable one exists.
# Whatever the source, the archive must match the checksum written here.
set -eu

id=@ID@
name=@NAME@
command_name=@COMMAND@
icon_size=@ICON_SIZE@
archive_name=@ARCHIVE@
archive_sha256=@ARCHIVE_SHA256@
download_url=@DOWNLOAD_URL@
stem=@STEM@

# Tilde resolves the current account; no environment overrides.
data_home=~/.local/share
bin_dir=~/.local/bin
install_dir="$data_home/$id"
bin_link="$bin_dir/$command_name"
desktop_dir="$data_home/applications"
desktop_file="$desktop_dir/$id.desktop"
icon_dir="$data_home/icons/hicolor/${icon_size}x${icon_size}/apps"

runtime_notice() { echo "$1"; }
runtime_fail() { echo "$1" >&2; exit 1; }

@RUNTIME_FUNCTIONS@

refresh_launcher() {
  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$desktop_dir" 2>/dev/null || true
  fi
  if [ -n "$icon_size" ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t "$data_home/icons/hicolor" 2>/dev/null || true
  fi
}

# Only a link this script made is removed or replaced.
owns_link() {
  [ -L "$bin_link" ] && [ "$(readlink "$bin_link")" = "$install_dir/$command_name" ]
}

usage() {
  echo "Usage: sh install.sh [<archive or URL>]"
  echo "       sh install.sh --uninstall"
  echo "Without an argument it installs $archive_name from beside this script,"
  if [ -n "$download_url" ]; then
    echo "or downloads it from $download_url."
  else
    echo 'so keep the two together, or name the archive.'
  fi
}

source_arg=''
case "${1:-}" in
  '') ;;
  --uninstall)
    [ "$#" -eq 1 ] || { echo 'Unexpected arguments' >&2; exit 2; }
    if [ -L "$install_dir" ]; then
      echo "Refusing to remove the linked application: $install_dir" >&2
      exit 1
    fi
    if owns_link; then rm -f "$bin_link"; fi
    rm -f "$desktop_file"
    [ -z "$icon_size" ] || rm -f "$icon_dir/$id.png"
    rm -rf "$install_dir"
    refresh_launcher
    echo "$name is uninstalled. Its saved data and gpui-shell are kept."
    exit 0 ;;
  --help | -h) usage; exit 0 ;;
  -*) echo "Unknown option: $1" >&2; exit 2 ;;
  *) source_arg=$1 ;;
esac
[ "$#" -le 1 ] || { echo 'Unexpected arguments' >&2; exit 2; }

for utility in install mktemp readlink tar; do
  command -v "$utility" >/dev/null 2>&1 || { echo "Missing dependency: $utility" >&2; exit 1; }
done
if [ -L "$install_dir" ]; then
  echo "Refusing to replace the linked application: $install_dir" >&2
  exit 1
fi
if { [ -e "$bin_link" ] || [ -L "$bin_link" ]; } && ! owns_link; then
  echo "Refusing to replace $bin_link, which this application did not install." >&2
  exit 1
fi

# Where the archive comes from: the argument, the file beside this script, or
# the release. Piped into `sh`, $0 is the shell, so there is no "beside".
source=''
case "$source_arg" in
  http://* | https://*) source=$source_arg ;;
  '')
    case "$0" in
      *.sh)
        if [ -f "$0" ] && [ -f "$(dirname "$0")/$archive_name" ]; then
          source="$(cd "$(dirname "$0")" && pwd -P)/$archive_name"
        fi ;;
    esac
    if [ -z "$source" ] && [ -n "$download_url" ]; then
      source="${download_url%/}/$archive_name"
    fi
    if [ -z "$source" ]; then
      echo "Cannot find $archive_name." >&2
      usage >&2
      exit 1
    fi ;;
  *)
    [ -f "$source_arg" ] || { echo "No such archive: $source_arg" >&2; exit 1; }
    source=$source_arg ;;
esac

# Now rather than at the first launch, where a download has no terminal.
# Before any file changes, so a failed download leaves an installation as it was.
ensure_runtime
echo "Using $("$runtime" --version) at $runtime"

mkdir -p "$data_home" "$bin_dir" "$desktop_dir"
stage=$(mktemp -d "$data_home/.$id.XXXXXX")
trap 'rm -rf "$stage"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

case "$source" in
  http://* | https://*)
    command -v curl >/dev/null 2>&1 || { echo 'Missing dependency: curl' >&2; exit 1; }
    echo "Downloading $source"
    curl --fail --silent --show-error --location --retry 3 "$source" -o "$stage/$archive_name" ;;
  *) cp "$source" "$stage/$archive_name" ;;
esac

if command -v sha256sum >/dev/null 2>&1; then
  actual=$(sha256sum "$stage/$archive_name" | awk '{ print $1 }')
elif command -v shasum >/dev/null 2>&1; then
  actual=$(shasum -a 256 "$stage/$archive_name" | awk '{ print $1 }')
else
  echo 'Missing a SHA-256 verifier (sha256sum or shasum).' >&2
  exit 1
fi
if [ "$actual" != "$archive_sha256" ]; then
  echo "$source does not match the $archive_name this script was written for." >&2
  exit 1
fi

mkdir "$stage/unpacked"
tar -xzf "$stage/$archive_name" -C "$stage/unpacked"
here="$stage/unpacked/$stem"
if [ ! -f "$here/$command_name" ] || [ ! -f "$here/app/gpui-shell.json" ]; then
  echo "$archive_name does not hold $stem/$command_name and its app." >&2
  exit 1
fi

# Copy first, then swap: a failed copy leaves the installed version untouched.
mkdir "$stage/new"
install -m 755 "$here/$command_name" "$stage/new/$command_name"
cp -R "$here/app" "$stage/new/app"
if [ -e "$install_dir" ]; then mv "$install_dir" "$stage/previous"; fi
if ! mv "$stage/new" "$install_dir"; then
  [ ! -e "$stage/previous" ] || mv "$stage/previous" "$install_dir"
  exit 1
fi

ln -sfn "$install_dir/$command_name" "$bin_link"

icon=application-x-executable
if [ -n "$icon_size" ] && [ -f "$here/$command_name.png" ]; then
  mkdir -p "$icon_dir"
  install -m 644 "$here/$command_name.png" "$icon_dir/$id.png"
  icon=$id
fi

# Desktop Exec quoting has its own grammar; escape the account path literally.
desktop_exec=$(printf '%s' "$install_dir/$command_name" | sed 's/\\/\\\\/g; s/"/\\"/g; s/`/\\`/g; s/\$/\\$/g; s/%/%%/g')
cat > "$stage/$id.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=$name
Exec="$desktop_exec"
Icon=$icon
Terminal=false
Categories=Utility;
StartupWMClass=$id
StartupNotify=true
DESKTOP
install -m 644 "$stage/$id.desktop" "$desktop_file"
refresh_launcher

echo "$name is installed in $install_dir."
echo 'Open it from your application launcher, or run:'
echo "  $bin_link"
case ":${PATH:-}:" in
  *":$bin_dir:"*) ;;
  *) echo "Add $bin_dir to PATH to run it as \`$command_name\`." ;;
esac
)BUNDLESCRIPT";

extern const char kBundleMacosLauncherSh[] =
    R"BUNDLESCRIPT(#!/bin/sh
# Starts @NAME@ with an installed gpui-shell. Written by `gpui-shell bundle`.
#
# Finder starts this with PATH=/usr/bin:/bin:/usr/sbin:/sbin, which holds
# everything used here: curl, shasum, tar and osascript.
set -eu

# Messages reach osascript as arguments, never as script text.
runtime_notice() {
  osascript - "$1" @NAME@ >/dev/null 2>&1 <<'APPLESCRIPT' || true
on run argv
  display notification (item 1 of argv) with title (item 2 of argv)
end run
APPLESCRIPT
}

runtime_fail() {
  osascript - "$1" @NAME@ >/dev/null 2>&1 <<'APPLESCRIPT' || true
on run argv
  display alert ((item 2 of argv) & " cannot start") message (item 1 of argv) as critical
end run
APPLESCRIPT
  echo "$1" >&2
  exit 1
}

@RUNTIME_FUNCTIONS@

# Contents/MacOS/<name> beside Contents/Resources/app.
dir=$(cd "$(dirname "$0")" && pwd -P)
ensure_runtime
exec "$runtime" run --dir "$dir/../Resources/app" --packaged
)BUNDLESCRIPT";

} // namespace gpui::shell
