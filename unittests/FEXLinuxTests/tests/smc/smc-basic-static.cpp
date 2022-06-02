// append cxxflags: -static
// append ldflags: -static -z execstack
auto args = "mmap, stack, data_sym, text_sym";

#define EXECSTACK
//#define OMAGIC // when the g++ driver is used to link, -Wl,--omagic breaks -static, so this can't be tested
#include "smc-basic.inl"