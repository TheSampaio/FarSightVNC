# libjpeg: C static library (third-party, unmodified), mirrors libjpeg.vcxproj.
# No PCH for C sources and no extra include directories, as in the .vcxproj.
add_library(libjpeg STATIC
  jaricom.c
  jcapimin.c
  jcapistd.c
  jcarith.c
  jccoefct.c
  jccolor.c
  jcdctmgr.c
  jchuff.c
  jcinit.c
  jcmainct.c
  jcmarker.c
  jcmaster.c
  jcomapi.c
  jcparam.c
  jcprepct.c
  jcsample.c
  jctrans.c
  jdapimin.c
  jdapistd.c
  jdarith.c
  jdatadst.c
  jdatasrc.c
  jdcoefct.c
  jdcolor.c
  jddctmgr.c
  jdhuff.c
  jdinput.c
  jdmainct.c
  jdmarker.c
  jdmaster.c
  jdmerge.c
  jdpostct.c
  jdsample.c
  jdtrans.c
  jerror.c
  jfdctflt.c
  jfdctfst.c
  jfdctint.c
  jidctflt.c
  jidctfst.c
  jidctint.c
  jmemmgr.c
  jmemnobs.c
  jquant1.c
  jquant2.c
  jutils.c)

fsv_configure_target(libjpeg)
target_compile_definitions(libjpeg PRIVATE _LIB _CRT_SECURE_NO_WARNINGS)
