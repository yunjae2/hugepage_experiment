#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define BASEPAGE_SIZE	4096
#define HUGEPAGE_SIZE	(2 * 1024 * 1024)
#define TLB_SIZE	(512 * 4 * 1024)	// 512 entries with 4K
#define HUGETLB_SIZE	(32 * 2 * 1024 * 1024)	// 32 entries with 2M

int *object;

/*
 * We have two type of options;
 * 1. Use huge page?
 * 2. Size of object
 */

void init_object(size_t size)
{
	int i;

	if (posix_memalign((void **)&object, BASEPAGE_SIZE, size)) {
		printf("Object allocation failed!\n");
		exit(1);
	}

	for (i = 0; i * sizeof(int) < size; i++)
		object[i] = i + 1;
}

void madvise_object(size_t size, int huge)
{
	if (!huge)
		return;

	if (madvise(object, size, MADV_HUGEPAGE)) {
		printf("madvise() failed!\n");
		exit(1);
	}
}

void pollute_tlb(int huge)
{
	char *pollute_data;
	int tlb_size, page_size;
	int i;

	if (huge) {
		tlb_size = HUGETLB_SIZE;
		page_size = HUGEPAGE_SIZE;
	} else {
		tlb_size = TLB_SIZE;
		page_size = BASEPAGE_SIZE;
	}
	posix_memalign((void **)&pollute_data, page_size, tlb_size);

	for (i = 0; i < tlb_size; i++)
		pollute_data[i] = (char) i;

	free(pollute_data);
}

void access_object(size_t size)
{
	int i;
	int nr_iters = 0;

	for (i = 0; nr_iters * sizeof(int) < size; i = object[i])
		nr_iters++;
}

void free_object(void)
{
	free(object);
}

int main(int argc, char **argv)
{
	int madvise_huge;
	size_t size;

	if (argc != 3) {
		printf("Usage: %s <base|huge> <object size (MiB)>\n", argv[0]);
		exit(1);
	}
	if (!strcmp(argv[1], "base"))
		madvise_huge = 0;
	else if (!strcmp(argv[1], "huge"))
		madvise_huge = 1;
	else {
		printf("Usage: %s <base|huge> <object size (MiB)>\n", argv[0]);
		exit(1);
	}
	size = atoi(argv[2]) * 1024 * 1024;

	init_object(size);
	madvise_object(size, madvise_huge);
	pollute_tlb(madvise_huge);
	access_object(size);
	free_object();

	return 0;
}
