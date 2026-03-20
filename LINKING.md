# Linking And LGPL Compliance

`node-smbx` bundles wrapper code plus vendored `libsmb2` sources.

- The wrapper code in this repository is distributed under the MIT license.
- `libsmb2` remains under LGPL-2.1-or-later.
- The npm package ships the vendored `libsmb2` source, `binding.gyp`, and native wrapper sources so consumers can relink the package with a modified `libsmb2`.

To relink:

1. Replace or modify the contents under `deps/libsmb2/`.
2. Run `npm install` or `node-gyp rebuild`.
3. The addon will be rebuilt against the modified library sources.
