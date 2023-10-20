
include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_ivs

mod_LTLIBRARIES = mod_ivs.la
mod_ivs_la_SOURCES  = mod_ivs.c utils.c ivs_playback.c ivs_events.c ivs_curl.c js_ivs_wrp.c js_ivs_hlp.c ivs_qjs.c js_ivs.c js_file.c js_curl.c js_session.c js_chatgpt.c
mod_ivs_la_CFLAGS   = $(AM_CFLAGS) -I/opt/quickjs/include/quickjs -I. -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-label -Wno-declaration-after-statement -Wno-pedantic -Wno-switch
mod_ivs_la_LIBADD   = $(switch_builddir)/libfreeswitch.la /opt/quickjs/lib/quickjs/libquickjs.lto.a
mod_ivs_la_LDFLAGS  = -avoid-version -module -no-undefined -shared

$(am_mod_ivs_la_OBJECTS): mod_ivs.h
