// append ldflags: -z execstack
auto args = "stack, data_sym, text_sym";

#define EXECSTACK
#include "smc-1.inl"