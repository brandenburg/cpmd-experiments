#include <stdio.h>
#include <stdlib.h>

#include <time.h>

#define NUM_VARS 5000000

long data[NUM_VARS];

int main(int argc, char** argv)
{
	int i;
	long sum;
	srand(time(NULL));
	while (1) {
		for (i = 0; i < NUM_VARS; i++)
			data[i] = rand();
		sum = 0;
		for (i = 0; i < NUM_VARS; i++)
			sum += (i % 2 ? 1 : -1) * data[i];
		for (i = NUM_VARS - 1; i >= 0; i--)
			sum += (i % 2 ? -1 : 1) * 100  /  (data[i] ? data[i] : 1);
		if (argc > 1)
			printf("sum: %ld\n", sum);
	}
}
