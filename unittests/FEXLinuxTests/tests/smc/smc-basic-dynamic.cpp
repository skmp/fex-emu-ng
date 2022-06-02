// append ldflags: -z execstack
auto args = "mmap, stack, data_sym, text_sym";

#define EXECSTACK
#include "smc-basic.inl"