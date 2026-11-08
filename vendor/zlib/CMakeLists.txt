# zlib: C static library (third-party, unmodified), mirrors zlib.vcxproj.
# No PCH for C sources and no extra include directories, as in the .vcxproj.
add_library(zlib STATIC
  adler32.c
  compress.c
  crc32.c
  deflate.c
  gzlib.c
  infback.c
  inffast.c
  inflate.c
  inftrees.c
  trees.c
  uncompr.c
  zutil.c)

fsv_configure_target(zlib)
target_compile_definitions(zlib PRIVATE _LIB)
