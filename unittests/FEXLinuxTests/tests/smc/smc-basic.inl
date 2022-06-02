#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

char data_sym[16384];
char text_sym[16384] __attribute__((section(".text")));

int test(char *code, const char *name) {
  // mov eax, imm32
  code[0] = 0xB8;
  code[1] = 0xAA;
  code[2] = 0xBB;
  code[3] = 0xCC;
  code[4] = 0xDD;

  // ret
  code[5] = 0xC3;

  auto fn = (int (*)())code;
  auto e1 = fn();
  
  // patch imm
  code[3] = 0xFE;
  auto e2 = fn();

  mprotect(code, 4096, PROT_READ | PROT_EXEC);

  mprotect(code, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);

  // patch imm
  code[3] = 0xF3;

  mprotect(code, 4096, PROT_READ | PROT_EXEC);

  auto e3 = fn();

  mprotect(code, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);

  // patch imm
  code[3] = 0xF1;

  auto e4 = fn();

  int failure_set = 0;

  failure_set |= (e1 != 0xDDCCBBAA) << 0;
  printf("%s-1: %X, %s\n", name, e1, e1 != 0xDDCCBBAA ? "FAIL" : "PASS");
  failure_set |= (e2 != 0xDDFEBBAA) << 1;
  printf("%s-2: %X, %s\n", name, e2, e2 != 0xDDFEBBAA ? "FAIL" : "PASS");
  failure_set |= (e3 != 0xDDF3BBAA) << 2;
  printf("%s-3: %X, %s\n", name, e3, e3 != 0xDDF3BBAA ? "FAIL" : "PASS");
  failure_set |= (e4 != 0xDDF1BBAA) << 3;
  printf("%s-4: %X, %s\n", name, e4, e4 != 0xDDF1BBAA ? "FAIL" : "PASS");

  return failure_set;
}

int main(int argc, char *argv[]) {

  if (argc == 2) {

    if (strcmp(argv[1], "mmap") == 0) {
      auto code = (char *)mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, 0, 0);
      return test(code, "mmap");
    } else if (strcmp(argv[1], "stack") == 0) {
      // stack, depends on -z execstack or mprotect
      char stack[16384];
      auto code = (char *)(((uintptr_t)stack + 4095) & ~4095);

#if !defined(EXECSTACK)
      mprotect(code, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif

      return test(code, "stack");
    } else if (strcmp(argv[1], "data_sym") == 0) {
      // data_sym, must use mprotect
      auto code = (char *)(((uintptr_t)data_sym + 4095) & ~4095);
      mprotect(code, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
      return test(code, "data_sym");
    } else if (strcmp(argv[1], "text_sym") == 0) {
      // text_sym, depends on -Wl,omagic or mprotect
      auto code = (char *)(((uintptr_t)text_sym + 4095) & ~4095);

#if !defined(OMAGIC)
      mprotect(code, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif

      return test(code, "text_sym");
    }
  }

  printf("Invalid arguments\n");
  printf("please specify one of %s\n", args);

  return -1;
}
