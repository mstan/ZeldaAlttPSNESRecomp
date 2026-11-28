# Title metadata and payload requirements for the engine release driver.
APP_NAME="ZeldaALttP"
RELEASE_SLUG="ZeldaALttPSNESRecomp"        # matches the windows zip prefix
CMAKE_TARGET="ZeldaALttPSNESRecomp"
ROM_EXTS="sfc smc"
EXTRA_ARGS=""
REGEN_CMD="bash tools/regen.sh --no-tests"
PREBUILD_CMD=""
POSTBUILD_CMD=""
BOXART="recomp/launcher/boxart.tga"          # AppImage icon source (optional)
EXTRA_PAYLOAD=()                             # repo-relative files -> usr/bin/
# Release-owned mod catalog: packaging fails if the built tree is missing one
# of these rather than shipping an AppImage with an empty Mods page.
REQUIRED_MOD_MANIFESTS=(
  "packages/zelda-alttp.enhancement.widescreen/1.0.0/manifest.toml"
)
PROD_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=OFF )
DEBUG_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=ON )

MOD_CATALOG_DIRS=(.)
