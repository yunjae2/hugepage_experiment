#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <unistd.h>
#include <asm/unistd.h>

#define BASEPAGE_SIZE	4096
#define HUGEPAGE_SIZE	(2 * 1024 * 1024)
#define TLB_SIZE	(512 * 4 * 1024)	// 512 entries with 4K
#define HUGETLB_SIZE	(32 * 2 * 1024 * 1024)	// 32 entries with 2M

int *object;
int perf_fd;

/*
 * We have two type of options;
 * 1. Use huge page?
 * 2. Size of object
 */

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
		int cpu, int group_fd, unsigned long flags)
{
	int ret;

	ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd,
			flags);
	return ret;
}

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

void perf_record(void)
{
	struct perf_event_attr pe;

	__u64 perf_hw_cache_id = PERF_COUNT_HW_CACHE_DTLB;
	__u64 perf_hw_cache_op_id = PERF_COUNT_HW_CACHE_OP_READ;
	__u64 perf_hw_cache_op_result_id = PERF_COUNT_HW_CACHE_RESULT_MISS;

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_HW_CACHE;
	pe.size = sizeof(struct perf_event_attr);
	pe.disabled = 1;
	pe.exclude_hv = 1;
	pe.config = (perf_hw_cache_id) | (perf_hw_cache_op_id << 8) |
		(perf_hw_cache_op_result_id << 16);

	perf_fd = perf_event_open(&pe, 0, 0, -1, 0);
	if (perf_fd == -1) {
		fprintf(stderr, "Error opening leader %llx\n", pe.config);
		exit(EXIT_FAILURE);
	}

	ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0);
}

void perf_report(void)
{
	long long count;

	ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0);
	read(perf_fd, &count, sizeof(long long));

	printf("dtlb misses: %lld\n", count);

	close(perf_fd);
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
	perf_record();
	access_object(size);
	perf_report();
	free_object();

	return 0;
}
