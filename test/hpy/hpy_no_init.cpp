// Test fixture: a shared library named like an HPy extension module
// (hpy_no_init.hpy.so) that does not export HPyInit_hpy_no_init. Importing it
// must fail with ImportError, and the loader must not keep the library open.
extern "C" int hpy_no_init_marker() { return 0; }
