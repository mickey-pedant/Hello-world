#include <stdio.h>
#define max(a, b) \
	({ __typeof__ (a) _a = a;\
	   __typeof__(b) _b = b;\
	   _a > _b ? _a : _b; })

int main()
{
	int a = max(10,20);
	printf("%d\n", a);
	return 0;
}
