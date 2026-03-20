{
  "variables": {
    "libsmb2_sources": [
      "deps/libsmb2/lib/aes.c",
      "deps/libsmb2/lib/aes_reference.c",
      "deps/libsmb2/lib/aes128ccm.c",
      "deps/libsmb2/lib/alloc.c",
      "deps/libsmb2/lib/asn1-ber.c",
      "deps/libsmb2/lib/compat.c",
      "deps/libsmb2/lib/dcerpc.c",
      "deps/libsmb2/lib/dcerpc-lsa.c",
      "deps/libsmb2/lib/dcerpc-srvsvc.c",
      "deps/libsmb2/lib/errors.c",
      "deps/libsmb2/lib/hmac.c",
      "deps/libsmb2/lib/hmac-md5.c",
      "deps/libsmb2/lib/init.c",
      "deps/libsmb2/lib/libsmb2.c",
      "deps/libsmb2/lib/md4c.c",
      "deps/libsmb2/lib/md5.c",
      "deps/libsmb2/lib/ntlmssp.c",
      "deps/libsmb2/lib/pdu.c",
      "deps/libsmb2/lib/sha1.c",
      "deps/libsmb2/lib/sha224-256.c",
      "deps/libsmb2/lib/sha384-512.c",
      "deps/libsmb2/lib/smb2-cmd-close.c",
      "deps/libsmb2/lib/smb2-cmd-create.c",
      "deps/libsmb2/lib/smb2-cmd-echo.c",
      "deps/libsmb2/lib/smb2-cmd-error.c",
      "deps/libsmb2/lib/smb2-cmd-flush.c",
      "deps/libsmb2/lib/smb2-cmd-ioctl.c",
      "deps/libsmb2/lib/smb2-cmd-lock.c",
      "deps/libsmb2/lib/smb2-cmd-logoff.c",
      "deps/libsmb2/lib/smb2-cmd-negotiate.c",
      "deps/libsmb2/lib/smb2-cmd-notify-change.c",
      "deps/libsmb2/lib/smb2-cmd-oplock-break.c",
      "deps/libsmb2/lib/smb2-cmd-query-directory.c",
      "deps/libsmb2/lib/smb2-cmd-query-info.c",
      "deps/libsmb2/lib/smb2-cmd-read.c",
      "deps/libsmb2/lib/smb2-cmd-session-setup.c",
      "deps/libsmb2/lib/smb2-cmd-set-info.c",
      "deps/libsmb2/lib/smb2-cmd-tree-connect.c",
      "deps/libsmb2/lib/smb2-cmd-tree-disconnect.c",
      "deps/libsmb2/lib/smb2-cmd-write.c",
      "deps/libsmb2/lib/smb2-data-file-info.c",
      "deps/libsmb2/lib/smb2-data-filesystem-info.c",
      "deps/libsmb2/lib/smb2-data-security-descriptor.c",
      "deps/libsmb2/lib/smb2-data-reparse-point.c",
      "deps/libsmb2/lib/smb2-share-enum.c",
      "deps/libsmb2/lib/smb3-seal.c",
      "deps/libsmb2/lib/smb2-signing.c",
      "deps/libsmb2/lib/socket.c",
      "deps/libsmb2/lib/spnego-wrapper.c",
      "deps/libsmb2/lib/sync.c",
      "deps/libsmb2/lib/timestamps.c",
      "deps/libsmb2/lib/unicode.c",
      "deps/libsmb2/lib/usha.c"
    ]
  },
  "targets": [
    {
      "target_name": "smbx",
      "sources": [
        "<@(libsmb2_sources)",
        "native/addon.cc",
        "native/smb_connection.cc"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "native",
        "deps/libsmb2/include",
        "deps/libsmb2/include/smb2",
        "deps/libsmb2/lib"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS",
        "NODE_ADDON_API_DISABLE_DEPRECATED",
        "HAVE_CONFIG_H",
        "_WINDOWS",
        "WIN32_LEAN_AND_MEAN",
        "_CRT_SECURE_NO_WARNINGS",
        "NEED_GETLOGIN_R",
        "NEED_GETPID",
        "NEED_RANDOM",
        "NEED_SRANDOM"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "libraries": [
        "-lws2_32"
      ],
      "cflags_cc!": [
        "-fno-exceptions"
      ],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 0,
          "AdditionalOptions": [
            "/std:c++20"
          ]
        }
      }
    }
  ]
}
