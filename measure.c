#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <time.h>
#include <sched.h>

#define BASEPAGE_SIZE	4096
#define HUGEPAGE_SIZE	(2 * 1024 * 1024)
#define TLB_SIZE	(512 * 4 * 1024)	// 512 entries with 4K
#define HUGETLB_SIZE	(32 * 2 * 1024 * 1024)	// 32 entries with 2M

#define EVENT_DTLB_LOAD_MISSES		0x08
#define EVENT_DTLB_MISSES		0x49
#define EVENT_ITLB_MISSES		0x85
#define EVENT_ITLB_FLUSH		0xAE

#define UMASK_ANY			0x01
#define UMASK_WALK_COMPLETED		0x02
#define UMASK_WALK_CYCLES		0x04
#define UMASK_STLB_HIT			0x10
#define UMASK_PDE_MISS			0x20
#define UMASK_LARGE_WALK_COMPLETED	0x80


int *object;
int perf_fd;

/*
 * We have three type of options;
 * 1. Use huge page?
 * 2. Size of object
 * 3. Access type: sequential? random?
 */

void print_interval(struct timespec *start, struct timespec *end)
{
	time_t diff_sec;
	long diff_nsec;

	diff_sec = end->tv_sec - start->tv_sec;
	diff_nsec = end->tv_nsec - start->tv_nsec;

	if (diff_nsec < 0) {
		diff_nsec += 1000 * 1000 * 1000;
		diff_sec -= 1;
	}

	printf("%ld.%06lds\n", diff_sec, diff_nsec / 1000);
}

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
		int cpu, int group_fd, unsigned long flags)
{
	int ret;

	ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd,
			flags);
	return ret;
}

void init_object(size_t size, int sequential, int huge)
{
	int i, swap_with;
	int from, to;
	int *rand_seq;
	int temp;
	struct timespec start, end;

	int nr_entries = size / sizeof(int);

	clock_gettime(CLOCK_REALTIME, &start);

	if (posix_memalign((void **)&object, BASEPAGE_SIZE, size)) {
		printf("Object allocation failed!\n");
		exit(1);
	}

	if (huge) {
		if (madvise(object, size, MADV_HUGEPAGE)) {
			printf("madvise() failed!\n");
			exit(1);
		}
	}

	memset(object, 0, size);
	clock_gettime(CLOCK_REALTIME, &end);

	if (sequential) {
		for (i = 0; i < nr_entries; i++)
			object[i] = (i + 1) % nr_entries;
	} else {
		srand(42);
		rand_seq = malloc(size);
		for (i = 0; i < nr_entries; i++) {
			rand_seq[i] = i;
		}

		for (i = 0; i < nr_entries; i++) {
			swap_with = rand() % nr_entries;
			if (i == swap_with)
				continue;
			temp = rand_seq[i];
			rand_seq[i] = rand_seq[swap_with];
			rand_seq[swap_with] = temp;
		}

		from = rand_seq[nr_entries - 1];
		for (i = 0; i < nr_entries; i++) {
			to = rand_seq[i];
			object[from] = to;
			from = to;
		}

		free(rand_seq);
	}

	printf("alloc time: ");
	print_interval(&start, &end);
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

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_RAW;
	pe.size = sizeof(struct perf_event_attr);
	pe.disabled = 1;
	pe.exclude_hv = 1;
	pe.config = (UMASK_ANY << 8) | EVENT_DTLB_MISSES;

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
	struct timespec start, end;

	clock_gettime(CLOCK_REALTIME, &start);

	for (i = 0; nr_iters * sizeof(int) < size; i = object[i])
		nr_iters++;

	clock_gettime(CLOCK_REALTIME, &end);
	printf("access time: ");
	print_interval(&start, &end);
}

void free_object(void)
{
	free(object);
}

int main(int argc, char **argv)
{
	int madvise_huge;
	int sequential;
	size_t size;
	cpu_set_t cpu_mask;
	CPU_ZERO(&cpu_mask);
	CPU_SET(0, &cpu_mask);
	if (sched_setaffinity(0, sizeof(cpu_mask), &cpu_mask)) {
		printf("Affinity set failed\n");
		exit(1);
	}

	if (argc != 4) {
		printf("Usage: %s <base|huge> <seq|rand> <object size (KiB)>\n",
				argv[0]);
		exit(1);
	}
	if (!strcmp(argv[1], "base"))
		madvise_huge = 0;
	else if (!strcmp(argv[1], "huge"))
		madvise_huge = 1;
	else {
		printf("Usage: %s <base|huge> <seq|rand> <object size (KiB)>\n",
				argv[0]);
		exit(1);
	}
	if (!strcmp(argv[2], "seq"))
		sequential = 1;
	else if (!strcmp(argv[2], "rand"))
		sequential = 0;
	else {
		printf("Usage: %s <base|huge> <seq|rand> <object size (KiB)>\n",
				argv[0]);
		exit(1);
	}

	size = atoi(argv[3]) * 1024;

	init_object(size, sequential, madvise_huge);
	pollute_tlb(madvise_huge);
	perf_record();
	access_object(size);
	perf_report();
	free_object();

	return 0;
}
