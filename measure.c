#include <stdio.h>
#include <stdlib.h>

/*
 * We have two type of options;
 * 1. Use huge page?
 * 2. Size of object
 */
int main(int argc, char **argv)
{
	if (argc != 3) {
		printf("Usage: %s <base|huge> <object size (MiB)>\n", argv[0]);
		exit(1);
	}
	/* TODO: Implement the body */

	return 0;
}
