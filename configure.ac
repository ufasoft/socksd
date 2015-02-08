AC_INIT(socksd, 1.0, support@ufasoft.com,, http://github.com/ufasoft/socksd)
AC_CONFIG_MACRO_DIR([m4])
m4_include([m4/common.m4])


AC_CONFIG_SRCDIR([socksd.cpp])


AC_CHECK_LIB([ext], [AfxTestEHsStub],		, [AC_MSG_ERROR([Library libext not found, install it from https://github./com/ufasoft/libext])])


AC_OUTPUT(Makefile)

AU_PRINT_SETTINGS

