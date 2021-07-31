// A toy example just to produce some stack traces.
#include<stdlib.h>

int f1(int);
char f2(int);
float f3(int);

int (*fp_f1)(int) = f1;
char (*fp_f2)(int) = f2;
float (*fp_f3)(int) = f3;

void malloc_free() {
  char* m = malloc(1);
  if (m)
    free(m);
}

int f1(int depth) {
  if (!depth--)
    return 0;
  
  if (depth < 3)
    malloc_free();

  if (depth % 3)
    return fp_f3(depth);

  if (depth % 2)
    return fp_f2(depth);

  return fp_f3(depth);
}

char f2(int depth) {
  if (!depth--)
    return 0;

  if (depth < 3)
    malloc_free();

  if (depth % 3)
    return f3(depth);

  if (depth % 2)
    return f2(depth);

  return f3(depth);
}

float f3(int depth) {
  if (!depth--)
    return 0;

  if (depth < 3)
    malloc_free();

  if (depth % 2)
    return fp_f2(depth);

  if (depth % 3)
    return fp_f3(depth);

  return fp_f1(depth);

}

int main() {
  f1(32);
  f1(31);
  f1(30);
  
  f2(32);
  f2(31);
  f2(30);
  
  f3(32);
  f3(31);
  f3(30);

  return 0;
}
