{
  "variables": {
    "libsmb2_sources": [
      "vendor/libsmb2/lib/aes.c",
      "vendor/libsmb2/lib/aes_reference.c",
      "vendor/libsmb2/lib/aes128ccm.c",
      "vendor/libsmb2/lib/alloc.c",
      "vendor/libsmb2/lib/asn1-ber.c",
      "vendor/libsmb2/lib/compat.c",
      "vendor/libsmb2/lib/dcerpc.c",
      "vendor/libsmb2/lib/dcerpc-lsa.c",
      "vendor/libsmb2/lib/dcerpc-srvsvc.c",
      "vendor/libsmb2/lib/errors.c",
      "vendor/libsmb2/lib/hmac.c",
      "vendor/libsmb2/lib/hmac-md5.c",
      "vendor/libsmb2/lib/init.c",
      "vendor/libsmb2/lib/libsmb2.c",
      "vendor/libsmb2/lib/md4c.c",
      "vendor/libsmb2/lib/md5.c",
      "vendor/libsmb2/lib/ntlmssp.c",
      "vendor/libsmb2/lib/pdu.c",
      "vendor/libsmb2/lib/sha1.c",
      "vendor/libsmb2/lib/sha224-256.c",
      "vendor/libsmb2/lib/sha384-512.c",
      "vendor/libsmb2/lib/smb2-cmd-close.c",
      "vendor/libsmb2/lib/smb2-cmd-create.c",
      "vendor/libsmb2/lib/smb2-cmd-echo.c",
      "vendor/libsmb2/lib/smb2-cmd-error.c",
      "vendor/libsmb2/lib/smb2-cmd-flush.c",
      "vendor/libsmb2/lib/smb2-cmd-ioctl.c",
      "vendor/libsmb2/lib/smb2-cmd-lock.c",
      "vendor/libsmb2/lib/smb2-cmd-logoff.c",
      "vendor/libsmb2/lib/smb2-cmd-negotiate.c",
      "vendor/libsmb2/lib/smb2-cmd-notify-change.c",
      "vendor/libsmb2/lib/smb2-cmd-oplock-break.c",
      "vendor/libsmb2/lib/smb2-cmd-query-directory.c",
      "vendor/libsmb2/lib/smb2-cmd-query-info.c",
      "vendor/libsmb2/lib/smb2-cmd-read.c",
      "vendor/libsmb2/lib/smb2-cmd-session-setup.c",
      "vendor/libsmb2/lib/smb2-cmd-set-info.c",
      "vendor/libsmb2/lib/smb2-cmd-tree-connect.c",
      "vendor/libsmb2/lib/smb2-cmd-tree-disconnect.c",
      "vendor/libsmb2/lib/smb2-cmd-write.c",
      "vendor/libsmb2/lib/smb2-data-file-info.c",
      "vendor/libsmb2/lib/smb2-data-filesystem-info.c",
      "vendor/libsmb2/lib/smb2-data-security-descriptor.c",
      "vendor/libsmb2/lib/smb2-data-reparse-point.c",
      "vendor/libsmb2/lib/smb2-share-enum.c",
      "vendor/libsmb2/lib/smb3-seal.c",
      "vendor/libsmb2/lib/smb2-signing.c",
      "vendor/libsmb2/lib/socket.c",
      "vendor/libsmb2/lib/spnego-wrapper.c",
      "vendor/libsmb2/lib/sync.c",
      "vendor/libsmb2/lib/timestamps.c",
      "vendor/libsmb2/lib/unicode.c",
      "vendor/libsmb2/lib/usha.c"
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
        "vendor/libsmb2/include",
        "vendor/libsmb2/include/smb2",
        "vendor/libsmb2/lib"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS",
        "NODE_ADDON_API_DISABLE_DEPRECATED",
        "HAVE_CONFIG_H"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "cflags_cc": [
        "-std=c++20"
      ],
      "cflags_cc!": [
        "-fno-exceptions"
      ],
      "conditions": [
        [
          "OS==\"win\"",
          {
            "defines": [
              "_WINDOWS",
              "WIN32_LEAN_AND_MEAN",
              "_CRT_SECURE_NO_WARNINGS",
              "NEED_GETLOGIN_R",
              "NEED_GETPID",
              "NEED_RANDOM",
              "NEED_SRANDOM"
            ],
            "libraries": [
              "-lws2_32"
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
      ]
    }
  ]
}
