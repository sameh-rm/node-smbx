# Packaging

The root package is intended to support:

- local path install from a clean checkout
- tarball install from `npm pack`
- prebuild loading via `node-gyp-build`
- native rebuild fallback via `node-gyp`

Local and tarball installs should both compile the addon for the current
platform when no matching prebuilt binary is present.
