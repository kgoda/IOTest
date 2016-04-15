/* iotest.c 
 *
 * Copyright (C) 2000- GODA Kazuo (The University of Tokyo)
 *
 * $Id: iotest.c,v 1.8 2007/09/27 10:04:59 kgoda Exp $
 *
 * Time-stamp: <2008-05-09 00:52:18 kgoda>
 */

#include "iotest.h"

/*
 *
 * Global variables
 *
 */

/*
 * Thread local variable
 */

struct iotest_aio_context_t {

    /* context id */
    int id;
    
    /* libaio context */
    io_context_t ctx;
    
    /* libaio io control block */
    struct iocb *iocbs[1];
    
    /* libaio transfer buffer */
    char *bufs[1];
    
    /* libaio event array */
    struct io_event events[1];
    
    /* io flag */
    int is_issued;

    /* Time stamp */
    struct timeval tv[2]; /* [0]:start, [1]:end */

};

struct iotest_thr_t {

    /* Thread index (pointing array index of iotest.child) */
    int id;
    
    /* Thread id (pthread implementation) */
    pthread_t thr_id;

    /* Aio context array */
    struct iotest_aio_context_t *acs;
    
    /* Time stamp */
    struct timeval tv[2]; /* [0]:start, [1]:end */

    /* Accumulated IO time */
    double acciotim;
    
    /* Maximum IO response time */
    double mxiotim;
    
    /* Number of IOs */
    double nio;
    
    /* Buffer */
    char *buf;

};

/*
 * Application global variable
 */

struct iotest_dev_t {

   /* Device or file */
    char *fname;
    
    /* File descriptor */
    int fd;
    
    /* Accumulated IO time */
    double acciotim;
    
    /* Maximum IO response time */
    double mxiotim;
    
    /* Number of IOs */
    double nio;    
};

struct iotest_t {

    /* Devices */
    int ndev;
    struct iotest_dev_t *dev;
    
    /* Access mode */
    int mode;
    
    /* I/O configuration */
    int blksiz;
    unsigned long ofst0, ofst1;
    int nio;

    /* General configuration */
    int verbose;
    int is_nonop;

    /* Child threads */
    int nthr;
    struct iotest_thr_t *child;

    /* Number of aio contexts */
    int naio;

    /* Time stamp */
    struct timeval tv[2]; /* [0]:start, [1]:end */
    
} iotest;

/*
 * Constants
 */

#define MAX_NTHR 4096
#define MAX_NDEV 64
#define MAX_NAIO 4096

#define KILO     (1000)
#define MEGA     (KILO*KILO)
#define GIGA     (KILO*KILO*KILO)
#define KIBI     (1024)
#define MEBI     (KIBI*KIBI)
#define GIBI     (KIBI*KIBI*KIBI)

#define MODE_RANDOM     1
#define MODE_SEQUENTIAL 2
#define MODE_WRITE      64
#define MODE_DIRECTIO   1024
#define MODE_SYNC       2048

#define IS_SEQUENTIAL (iotest.mode & MODE_SEQUENTIAL)
#define IS_RANDOM     (iotest.mode & MODE_RANDOM)
#define IS_READ       (!(iotest.mode & MODE_WRITE))
#define IS_WRITE      (iotest.mode & MODE_WRITE)
#define IS_DIRECTIO   (iotest.mode & MODE_DIRECTIO)
#define IS_SYNCHRONOUS (iotest.mode & MODE_SYNC)

#define IS_SINGLE      (iotest.nthr == 1 ? 1 : 0)
#define IS_MULTIPLE    (!IS_SINGLE)


#define IS_NONOP      (0)

#define VERBOSE       (iotest.verbose)
#define VERBOSE1      (VERBOSE >= 1)
#define VERBOSE2      (VERBOSE >= 2)
#define VERBOSE3      (VERBOSE >= 3)
#define VERBOSE4      (VERBOSE >= 4)
#define VERBOSE5      (VERBOSE >= 5)

/*
 *
 * Forward declaration
 *
 */

static void *thread_handler(void *);
static void disktest(int);
static void disktest_libaio(int);

static void print_version(void);
static void print_usage(void);
static void print_config(void);
static void print_result(void);
static void print_result_child(int);
static void print_result_dev(int);
static unsigned long long getsize(char *);


/*
 *
 * Macros and inline functions
 *
 */

#define TIMEVAL_SUB(dest, base)             \
{                                           \
    (dest).tv_usec -= (base).tv_usec;       \
    (dest).tv_sec  -= (base).tv_sec;        \
    while((dest).tv_usec < 0){              \
        (dest).tv_usec += MEGA;             \
        (dest).tv_sec--;                    \
    }                                       \
}

#define TIMEVAL2DOUBLE(a)                   \
    ((double)(a).tv_sec + (double)(a).tv_usec / (double) MEGA)

static inline ssize_t iotest_pread(int fd, void *buf, size_t count, off_t offset)
{
    ssize_t ret;

    if(IS_NONOP)
	return(count);

 retry:
    ret = pread(fd, buf, count, offset);
    if(ret < 0){
	perror("iotest_read:pread()");
	exit(EXIT_FAILURE);
    }
    if(ret != count){
	count -= ret;
	buf += ret;
	goto retry;
    }

    if(VERBOSE5)
	printf("  pread(fd=%d, buf=%p, count=%lu, offset=%llu), ret=%ld\n",
	       fd, buf, count, (unsigned long long)offset, ret);

    return(ret);
}

static inline ssize_t iotest_pwrite(int fd, void *buf, size_t count, off_t offset)
{
    ssize_t ret;
    
    if(IS_NONOP)
	return(count);

 retry:
    ret = pwrite(fd, buf, count, offset);
    if(ret < 0){
	perror("iotest_write:pwrite()");
	exit(EXIT_FAILURE);
    }
    if(ret != count){
	count -= ret;
	buf += ret;
	goto retry;
    }

    if(VERBOSE5)
	printf("  pwrite(fd=%d, buf=%p, count=%lu, offset=%llu), ret=%ld\n",
	       fd, buf, count, (unsigned long long)offset, ret);

    return(ret);
}

#if 1

static inline int iotest_aio_check_io_ongoing(struct iotest_aio_context_t *ac)
{
    return(ac->is_issued);
}

static void iotest_aio_pread_done(io_context_t ctx, struct iocb *iocb, long res, long res2)
{
    if(VERBOSE5)
	printf("  aio_pread_done(res=%ld, res2=%ld),"
	       " fd=%d, op=%d, buf=%p, size=%ld, offset=%llu\n",
	       res, res2,
	       iocb->aio_fildes, iocb->aio_lio_opcode,
	       iocb->u.c.buf, iocb->u.c.nbytes, (unsigned long long)(iocb->u.c.offset));
    
    if(res < 0){
	errno = - res;
	perror("iotest_aio_pread_done:");
	exit(EXIT_FAILURE);
    }
    
    if(res != iocb->u.c.nbytes){
	fprintf(stderr, "aio_pread_done: Read operation partially completed. %ld bytes to be read, %ld actually read.\n", iocb->u.c.nbytes, res);
	exit(EXIT_FAILURE);
    }
    
    return;
}

static void iotest_aio_pwrite_done(io_context_t ctx, struct iocb *iocb, long res, long res2)
{
    if(VERBOSE5)
	printf("  iotest_aio_pwrite_done(res=%ld, res2=%ld),"
	       " fd=%d, op=%d, buf=%p, size=%ld, offset=%llu\n",
	       res, res2,
	       iocb->aio_fildes, iocb->aio_lio_opcode,
	       iocb->u.c.buf, iocb->u.c.nbytes, (unsigned long long)(iocb->u.c.offset));
    
    if(res < 0){
	errno = - res;
	perror("iotest_aio_pwrite_done:");
	exit(EXIT_FAILURE);
    }
    
    if(res != iocb->u.c.nbytes){
	fprintf(stderr, "iotest_aio_pwrite_done: Write operation partially completed. %ld bytes to be written, %ld actually written.\n", iocb->u.c.nbytes, res);
	exit(EXIT_FAILURE);
    }
    
    return;
}

static inline int iotest_aio_pread(struct iotest_aio_context_t *ac,
				   int fd, void *buf, size_t count, off_t offset)
{
    int ret;

    if(IS_NONOP)
	return(count);

    io_prep_pread(ac->iocbs[0], fd, buf, count, offset);
    io_set_callback(ac->iocbs[0], iotest_aio_pread_done);

    ret = io_submit(ac->ctx, 1, ac->iocbs);
    if(ret != 1){
	errno = - ret;
	perror("iotest_aio_pread:io_submit()");
	exit(EXIT_FAILURE);
    }
    
    if(VERBOSE5)
	printf("  aio_pread(fd=%d, buf=%p, count=%lu, offset=%llu), ac=%d(%p)\n",
	       fd, buf, count, (unsigned long long)offset, ac->id, ac);

    ac->is_issued = 1;
    
    return(count);
}

static inline int iotest_aio_pwrite(struct iotest_aio_context_t *ac,
				   int fd, void *buf, size_t count, off_t offset)
{
    int ret;

    if(IS_NONOP)
	return(count);

    io_prep_pwrite(ac->iocbs[0], fd, buf, count, offset);
    io_set_callback(ac->iocbs[0], iotest_aio_pwrite_done);

    ret = io_submit(ac->ctx, 1, ac->iocbs);
    if(ret != 1){
	errno = - ret;
	perror("iotest_aio_pwrite:io_submit()");
	exit(EXIT_FAILURE);
    }
    
    if(VERBOSE5)
	printf("  aio_pwrite(fd=%d, buf=%p, count=%lu, offset=%llu), ac=%d(%p)\n",
	       fd, buf, count, (unsigned long long)offset, ac->id, ac);

    ac->is_issued = 1;
    
    return(count);
}

static inline int iotest_aio_return(struct iotest_aio_context_t *ac)
{
    int ret;
    
    ret = io_getevents(ac->ctx, 0, 1, ac->events, NULL);

    if(ret){
	struct io_event *ev = ac->events + 0;
	io_callback_t callback = (io_callback_t)ev->data;
	callback(ac->ctx, ev->obj, ev->res, ev->res2);

	ac->is_issued = 0;
    }else{
#if 0
	if(VERBOSE5)
	    printf("  aio_return(), ret=%d\n", ret);
#endif
    }

    return(ret);
}
#endif

/*
 *
 * Main
 *
 */

int main(int argc, char **argv)
{
    int i;
    int opt;
    
    /*
     * Default Settings
     */

    iotest.mode    = 0;
    iotest.nthr    = 1;
    
    iotest.blksiz  = 65536;
    iotest.ofst0   = 0;
    iotest.ofst1   = 0;
    iotest.nio     = 0;

    iotest.verbose = 0;
    
    /*
     * command line
     */

    /* Options */
    
    while(1){
        if((opt = getopt(argc, argv, "RSWM:A:b:s:e:c:dpvV")) == EOF)
            break;
        switch(opt){
        case 'v':
            iotest.verbose++;
            break;
        case 'R':
            iotest.mode |= MODE_RANDOM;
            break;
        case 'S':
	    iotest.mode |= MODE_SEQUENTIAL;
            break;
        case 'W':
	    iotest.mode |= MODE_WRITE;
            break;
        case 'M':
            iotest.nthr = atoi(optarg);
            break;
        case 'A':
            iotest.naio = atoi(optarg);
            break;
        case 'b':
            iotest.blksiz = atoi(optarg);
            break;
        case 's':
	    iotest.ofst0 = atol(optarg);
            break;
        case 'e':
	    iotest.ofst1 = atol(optarg);
            break;
        case 'c':
	    iotest.nio = atof(optarg);
            break;
        case 'd':
	    iotest.mode |= MODE_DIRECTIO;
            break;
        case 'p':
	    iotest.mode |= MODE_SYNC;
            break;
        case 'V':
	    print_version();
            exit(EXIT_SUCCESS);	    
            break;
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    /* Devices */
    
    if(argc <= optind){
	fprintf(stderr, "Error: device_or_file is not specified.\n");
	print_usage();
	exit(EXIT_FAILURE);
    }
    iotest.ndev = argc - optind;

    iotest.dev = (struct iotest_dev_t *)malloc(sizeof(struct iotest_dev_t) * iotest.ndev);
    memset(iotest.dev, 0, sizeof(struct iotest_dev_t) * iotest.ndev);
    if(iotest.dev == NULL){
	perror("main:malloc()");
	exit(EXIT_FAILURE);
    }
    
    for(i=0; i<iotest.ndev; i++){
	iotest.dev[i].fname = (char *)strdup(argv[optind + i]);
    }

    /*
     * Check the correctness of options
     */

    if((IS_RANDOM & IS_SEQUENTIAL)){
	fprintf(stderr, "Error: -R and -S cannot be specified simultaneously.\n");
	print_usage();
	exit(EXIT_FAILURE);
    }
    if(!IS_RANDOM && !IS_SEQUENTIAL){
	fprintf(stderr, "Error: Access mode must be specified.\n");
	print_usage();
	exit(EXIT_FAILURE);
    }
    if(iotest.nthr > MAX_NTHR){
	fprintf(stderr, "Error: Multiplex degree exceeds system limit.\n");
	exit(EXIT_FAILURE);
    }
    if(iotest.naio > MAX_NAIO){
	fprintf(stderr, "Error: Number of aio contexts exceeds system limit.\n");
	exit(EXIT_FAILURE);
    }
    if(iotest.ndev > MAX_NDEV){
	fprintf(stderr, "Error: Number of specified devices exceeds system limits.\n");
	exit(EXIT_FAILURE);
    }

    if(!iotest.ofst1){
	unsigned long long size;
        if((size = getsize(iotest.dev[0].fname))){
	    iotest.ofst1 = size / iotest.blksiz;
	}else{
	    fprintf(stderr, "Error: iotest cannot check the size of %s. Please specify explicitly the access range by the use of options, -s and -e.\n", iotest.dev[0].fname);
	    exit(EXIT_FAILURE);
	}

	for(i=1; i<iotest.ndev; i++){
	    if(size != getsize(iotest.dev[i].fname)){
		fprintf(stderr, "Error: devices of different sizes are specified.\n");
		exit(EXIT_FAILURE);
	    }
	}
    }

    if(iotest.ofst0 > iotest.ofst1){
	fprintf(stderr, "Error: Access range is not correctly set. (%lu %lu)", iotest.ofst0, iotest.ofst1);
	exit(EXIT_FAILURE);
    }

    if(IS_SEQUENTIAL)
	if(!iotest.nio)
	    iotest.nio = iotest.ofst1 - iotest.ofst0;

    /*
     * Show the configuration
     */
    
    if(VERBOSE1)
	print_config();

    /*
     * Thread invokation
     */

    /* Memory allocation and file open*/

    iotest.child = (struct iotest_thr_t *)malloc(sizeof(struct iotest_thr_t) * iotest.nthr);
    memset(iotest.child, 0, sizeof(struct iotest_thr_t) * iotest.nthr);
    if(iotest.child == NULL){
	perror("main:malloc()");
	exit(EXIT_FAILURE);
    }
    
    for(i=0; i<iotest.nthr; i++){
	iotest.child[i].buf = (char *)valloc(iotest.blksiz);
	if(iotest.child[i].buf == NULL){
	    perror("main:valloc()");
	    exit(EXIT_FAILURE);
	}
	memset(iotest.child[i].buf, 0, iotest.blksiz);
    }

    for(i=0; i<iotest.ndev; i++){
	mode_t mode = 0;
	int flags;
	if(IS_WRITE)
	  flags = O_WRONLY;
	else
	  flags = O_RDONLY;
#ifdef __linux__
	if(IS_DIRECTIO)
	    flags |= O_DIRECT;
	if(IS_SYNCHRONOUS)
	    flags |= O_SYNC;
#endif
	iotest.dev[i].fd = open(iotest.dev[i].fname, flags, mode);
	
	if(iotest.dev[i].fd < 0){
	    perror("main:open()");
	    exit(EXIT_FAILURE);
	}
    }

    /* Invocation */

    gettimeofday(&(iotest.tv[0]), NULL);
    for(i=0; i<iotest.nthr; i++){
	
        if(VERBOSE4)
            printf("Invoking child thread[%d].\n", i);

	iotest.child[i].id = i;
        if(pthread_create(&(iotest.child[i].thr_id), NULL, thread_handler, (void *)&(iotest.child[i])) != 0){
            perror("main:pthread_create()");
            exit(EXIT_FAILURE);
        }
    }
    
    /*
     * Thread termination
     */

    /* Wait for thread termination */
    
    for(i=0; i<iotest.nthr; i++){
	if(VERBOSE4)
	    printf("Waiting child thread[%d] to terminate.\n", i);

        pthread_join(iotest.child[i].thr_id, NULL);
    }
    gettimeofday(&(iotest.tv[1]), NULL);
    
    /* Show result */
    
    print_result();


    /* File close and meory release */

    for(i=0; i<iotest.ndev; i++)
	close(iotest.dev[i].fd);
    for(i=0; i<iotest.nthr; i++)
	free(iotest.child[i].buf);
    free(iotest.child);

    return(EXIT_SUCCESS);
}

/*
 *
 * Subroutines
 *
 */

static void *thread_handler(void *arg)
{
    struct iotest_thr_t *thr = (struct iotest_thr_t *)arg;
    int id = thr->id;

    if(iotest.naio)
	disktest_libaio(id);
    else
	disktest(id);

    
    return(NULL);
}

static void disktest(int id)
{
    int i;
    struct iotest_thr_t *thr;

    thr = &(iotest.child[id]);
    
    /*
     * Begin
     */
    
    if(VERBOSE4)
	printf("TH[%d] starts.\n", id);

    gettimeofday(&(thr->tv[0]), NULL);

    /*
     * Loop
     */

    for(i=0; i<iotest.nio; i++){
	int nread;
	int devid;
	unsigned long long ofst;
	struct timeval tv[2];
	
	if(i==0){
	    if(IS_RANDOM)
		srand(time(0) + id * 13);
	}
	
	if(IS_RANDOM){
	    ofst = (unsigned long long)iotest.ofst0;
	    ofst += (unsigned long long)((unsigned long long)iotest.ofst1-(unsigned long long)iotest.ofst0)*rand()/(RAND_MAX+1.0);
	    ofst *=  iotest.blksiz;
	}else{
	    ofst = (iotest.ofst0 + i) * iotest.blksiz;
	}

	if(IS_RANDOM)
	    devid = (int)(((double)iotest.ndev)*rand()/(RAND_MAX+1.0));
	else
	    devid = thr->id % iotest.ndev;

	gettimeofday(&tv[0], NULL);
	    nread = iotest_pread(iotest.dev[devid].fd, thr->buf, 4096, (unsigned long long)1024*1000*1000*1000*1);
	    nread = iotest_pread(iotest.dev[devid].fd, thr->buf, 4096, (unsigned long long)1024*1000*1000*1000*2);
	    nread = iotest_pread(iotest.dev[devid].fd, thr->buf, 4096, (unsigned long long)1024*1000*1000*1000*3);
	    nread = iotest_pread(iotest.dev[devid].fd, thr->buf, 4096, (unsigned long long)1024*1000*1000*1000*4);
	    nread = iotest_pread(iotest.dev[devid].fd, thr->buf, 4096, (unsigned long long)1024*1000*1000*1000*5);
	exit(0);
	if(IS_READ)
	    nread = iotest_pread(iotest.dev[devid].fd, thr->buf, iotest.blksiz, ofst);
	else
	    nread = iotest_pwrite(iotest.dev[devid].fd, thr->buf, iotest.blksiz, ofst);
	gettimeofday(&tv[1], NULL);

	TIMEVAL_SUB(tv[1], tv[0]);
	thr->acciotim += TIMEVAL2DOUBLE(tv[1]);
	if(thr->mxiotim < TIMEVAL2DOUBLE(tv[1]))
	    thr->mxiotim = TIMEVAL2DOUBLE(tv[1]);
	thr->nio++;
	iotest.dev[devid].acciotim += TIMEVAL2DOUBLE(tv[1]);
	if(iotest.dev[devid].mxiotim < TIMEVAL2DOUBLE(tv[1]))
	    iotest.dev[devid].mxiotim = TIMEVAL2DOUBLE(tv[1]);
	iotest.dev[devid].nio++;
	
    } /* for(i) */
    
    /*
     * Finish
     */

    gettimeofday(&(thr->tv[1]), NULL);

    if(VERBOSE4)
	printf("TH[%d] ends.\n", id);
}


#ifdef __linux__
static void disktest_libaio(int id)
{
    int i;
    struct iotest_thr_t *thr;

    thr = &(iotest.child[id]);
    
    /*
     * Begin
     */
    
    if(VERBOSE4)
	printf("TH[%d] starts.\n", id);

    gettimeofday(&(thr->tv[0]), NULL);

    if((thr->acs = (struct iotest_aio_context_t *)malloc(sizeof(struct iotest_aio_context_t) * iotest.naio)) == NULL){
	perror("disktest_libaio:malloc():acs");
	exit(EXIT_FAILURE);
    }

    for(i=0; i<iotest.naio; i++){

	struct iotest_aio_context_t *ac = &(thr->acs[i]);
	int r;
	
	ac->id = i;
	
	memset(&(ac->ctx), 0, sizeof(io_context_t));
	if((r = io_setup(1, &(ac->ctx))) != 0){
	    errno = - r;
	    perror("disktest:io_setup()");
	    exit(EXIT_FAILURE);
	}

	if((ac->iocbs[0] = (struct iocb *)malloc(sizeof(struct iocb))) == NULL){
	    perror("disktest_libaio:malloc():iocbs");
	    exit(EXIT_FAILURE);
	}

	if((ac->bufs[0] = (char *)valloc(iotest.blksiz)) == NULL){
	    perror("disktest_libaio:malloc():bufs");
	    exit(EXIT_FAILURE);
	}	
    }

    /*
     * Loop
     */

    int cid = 0, nio_completed = 0, nio_issued = 0;

    while(1){
	int ret;
	struct iotest_aio_context_t *ac;
	ac = &(thr->acs[cid % iotest.naio]);
	cid++;
	if(!iotest_aio_check_io_ongoing(ac)){
	    /* Context can be processed. */

	    if(nio_issued < iotest.nio){
		
		i = nio_issued;
		
		if(1){
		    
		    int devid;
		    unsigned long long ofst;

		    if(i==0){
			if(IS_RANDOM)
			    srand(time(0));
		    }

		    if(IS_RANDOM){
			ofst = (unsigned long long)iotest.ofst0;
			ofst += (unsigned long long)((unsigned long long)iotest.ofst1-(unsigned long long)iotest.ofst0)*rand()/(RAND_MAX+1.0);
			ofst *=  iotest.blksiz;
		    }else{
			ofst = (iotest.ofst0 + i) * iotest.blksiz;
		    }

		    if(IS_RANDOM)
			devid = (int)(((double)iotest.ndev)*rand()/(RAND_MAX+1.0));
		    else
			devid = thr->id % iotest.ndev;
		    
		    gettimeofday(&(ac->tv[0]), NULL);
		    if(IS_READ)
			iotest_aio_pread(ac,
					 iotest.dev[devid].fd, ac->bufs[0], iotest.blksiz, ofst);
		    else
			iotest_aio_pwrite(ac,
					  iotest.dev[devid].fd, ac->bufs[0], iotest.blksiz, ofst);
		    gettimeofday(&(ac->tv[1]), NULL);
		    
		    TIMEVAL_SUB(ac->tv[1], ac->tv[0]);
		    thr->acciotim += TIMEVAL2DOUBLE(ac->tv[1]);
		    if(thr->mxiotim < TIMEVAL2DOUBLE(ac->tv[1]))
			thr->mxiotim = TIMEVAL2DOUBLE(ac->tv[1]);
		    thr->nio++;
		    iotest.dev[devid].acciotim += TIMEVAL2DOUBLE(ac->tv[1]);
		    if(iotest.dev[devid].mxiotim < TIMEVAL2DOUBLE(ac->tv[1]))
			iotest.dev[devid].mxiotim = TIMEVAL2DOUBLE(ac->tv[1]);
		    iotest.dev[devid].nio++;

		} /* if(1) */
		    
		nio_issued++;
	    }
	}else{
	    /* IO is still being operated. */
	}

	if((ret = iotest_aio_return(ac)))
	    nio_completed += ret;

	if(nio_completed >= iotest.nio)
	    break;
    }
    
    /*
     * Finish
     */

    gettimeofday(&(thr->tv[1]), NULL);

    if(VERBOSE4)
	printf("TH[%d] ends.\n", id);
}
#endif /* __linux__ */

/*
 * print_version():
 */

static void print_version(void)
{
    printf("%s %s\n", NAME, VERSION);
    printf("%s %s\n", AUTHOR, CONTACT);
}

/*
 * print_usage(): 
 */

static void print_usage(void)
{
    fputs("\
Usage: iotest [options] devices_or_files ... \n\
Description:\n\
  I/O device test\n\
Options (access mode):\n\
  -R     : random access\n\
  -S     : sequential access\n\
  -W     : write operation; unless set, read operation\n\
  -M <n> : multiplex degree of I/O threads; unless set, non-multiplexing\n\
Options (I/O configuration):\n\
  -b <n> : access block size (in bytes)\n\
  -s <n> : block offset (in blocks) to start with; unless set, 0\n\
  -e <n> : block offset (in blocks) to end with; unless set, size of device or file\n\
  -c <n> : number of I/O operations\n\
Options (OS dependent configuration):\n\
  -d <n> : direct mode, directly copying data from/to user space buffers\n\
  -p <n> : synchronous mode, physically synchronizing data\n\
Options (general configuration):\n\
  -v     : verbose mode\n\
  -n     : non-operation mode; does not really issue I/O\n\
",
	  stderr);
}

/*
 * print_config(): print application configuration parameters
 */

void print_config(void)
{
    int i;
    
    printf("\
************************************************************\n\
  iotest - Configuration\n\
************************************************************\n\
");
    
    printf("  Device(s)            : %d \n", iotest.ndev);
    for(i=0; i<iotest.ndev; i++)
	printf("                         %s\n", iotest.dev[i].fname);
    printf("  Access pattern       : %s %s\n",
	   IS_RANDOM ? "Fully random" : "Fully sequential",
	   IS_READ ? "read" : "write");
    printf("  IO mode option       : %s%s\n",
	   IS_DIRECTIO ? "O_DIRECT " : "",
	   IS_SYNCHRONOUS ? "O_SYNC " : "");
    printf("  Multiplexing         : %s (multiplex degree: %d)\n",
	   IS_MULTIPLE ? "Yes" : "No",
	   iotest.nthr);
    printf("  Aio                  : %s (number of contexts: %d)\n",
	   iotest.naio ? "Yes" : "No",
	   iotest.naio);
    printf("  Block size           : %7d [Byte]\n",
	   iotest.blksiz);
    printf("  Access region        : %12lu - %12lu (%12lu) [block]\n",
	   iotest.ofst0,
	   iotest.ofst1,
	   iotest.ofst1-iotest.ofst0);
    printf("                       : %12lld - %12lld (%12lld) [MB]\n",
	   (unsigned long long)iotest.ofst0 * iotest.blksiz / MEGA,
	   (unsigned long long)iotest.ofst1 * iotest.blksiz / MEGA,
	   (unsigned long long)(iotest.ofst1-iotest.ofst0) * iotest.blksiz / MEGA);
    printf("                       : %12lld - %12lld (%12lld) [MiB]\n",
	   (unsigned long long)iotest.ofst0 * iotest.blksiz / MEBI,
	   (unsigned long long)iotest.ofst1 * iotest.blksiz / MEBI,
	   (unsigned long long)(iotest.ofst1-iotest.ofst0) * iotest.blksiz / MEBI);
    printf("  Number of I/Os       : %12d [block] %12d [block/thread]\n",
	   iotest.nio * iotest.nthr,
	   iotest.nio);
    printf("                       : %12lld [MB]    %12lld [MB/thread]\n",
	   (unsigned long long)iotest.nio * iotest.blksiz / MEGA * iotest.nthr,
	   (unsigned long long)iotest.nio * iotest.blksiz / MEGA);
    printf("                       : %12lld [MiB]   %12lld [MiB/thread]\n",
	   (unsigned long long)iotest.nio * iotest.blksiz / MEBI * iotest.nthr,
    	   (unsigned long long)iotest.nio * iotest.blksiz / MEBI);
}

/*
 * print_result():
 */

static void print_result()
{
    int i;
    double sum_acciotim = 0, sum_mxiotim = 0;
    
    printf("\
************************************************************\n\
  iotest - Global result\n\
************************************************************\n\
");

    printf("  Exec. time           : %9.3f - %9.3f (%9.3f) [s]\n",
	   TIMEVAL2DOUBLE(iotest.tv[0]),
	   TIMEVAL2DOUBLE(iotest.tv[1]),
	   TIMEVAL2DOUBLE(iotest.tv[1]) - TIMEVAL2DOUBLE(iotest.tv[0]));

    printf("  Total throughput     : %9.3f [block/s]\n",
	   (double)iotest.nio * iotest.nthr /
	   (TIMEVAL2DOUBLE(iotest.tv[1]) - TIMEVAL2DOUBLE(iotest.tv[0])));
    printf("                       : %9.3f [MB/s]\n",
	   (double)iotest.nio * iotest.nthr * iotest.blksiz /
	   (TIMEVAL2DOUBLE(iotest.tv[1]) - TIMEVAL2DOUBLE(iotest.tv[0]))
	   / MEGA);
    printf("                       : %9.3f [MiB/s]\n",
	   (double)iotest.nio * iotest.nthr * iotest.blksiz /
	   (TIMEVAL2DOUBLE(iotest.tv[1]) - TIMEVAL2DOUBLE(iotest.tv[0]))
	   / MEBI);

    for(i=0; i<iotest.nthr; i++)
	sum_acciotim += iotest.child[i].acciotim;
    for(i=0; i<iotest.nthr; i++)
	if(sum_mxiotim < iotest.child[i].mxiotim)
	    sum_mxiotim = iotest.child[i].mxiotim;
    printf("  Avg. Resp. time      : %9.3f [ms/block]\n",
	   sum_acciotim * KILO / (iotest.nio * iotest.nthr));
    printf("  Max. Resp. time      : %9.3f [ms/block]\n",
	   sum_mxiotim * KILO);
    printf("  Accm. I/O time       : %9.3f [s]\n",
	   sum_acciotim);
    
    if(VERBOSE2){

	printf("\
************************************************************\n\
  iotest - Child result(s)\n\
************************************************************\n\
");
	
	for(i=0; i<iotest.nthr; i++)
	    print_result_child(i);
    }
    if(VERBOSE2){

	printf("\
************************************************************\n\
  iotest - Device result(s)\n\
************************************************************\n\
");
	
	for(i=0; i<iotest.ndev; i++)
	    print_result_dev(i);
    }
}

static void print_result_child(int id)
{
    struct iotest_thr_t *thr = &(iotest.child[id]);

    printf("  [%02d] Exec. time      : %9.3f - %9.3f (%9.3f) [s]\n",
	   id,
	   TIMEVAL2DOUBLE(thr->tv[0]),
	   TIMEVAL2DOUBLE(thr->tv[1]),
	   TIMEVAL2DOUBLE(thr->tv[1]) - TIMEVAL2DOUBLE(thr->tv[0]));

    printf("       Throughput      : %9.3f [block/s]\n",
	   (double)iotest.nio /
	   (TIMEVAL2DOUBLE(thr->tv[1]) - TIMEVAL2DOUBLE(thr->tv[0])));
    printf("                       : %9.3f [MB/s]\n",
	   (double)iotest.nio * iotest.blksiz /
	   (TIMEVAL2DOUBLE(thr->tv[1]) - TIMEVAL2DOUBLE(thr->tv[0]))
	   / MEGA);
    printf("                       : %9.3f [MiB/s]\n",
	   (double)iotest.nio * iotest.blksiz /
	   (TIMEVAL2DOUBLE(thr->tv[1]) - TIMEVAL2DOUBLE(thr->tv[0]))
	   / MEBI);

    printf("       Avg. Resp. time : %9.3f [ms/block]\n",
	   thr->acciotim * KILO / iotest.nio);
    printf("       Max. Resp. time : %9.3f [ms/block]\n",
	   thr->mxiotim * KILO);
    printf("       Accm. I/O time  : %9.3f [s]\n",
	   thr->acciotim);
}

static void print_result_dev(int id)
{
    struct iotest_dev_t *dev = &(iotest.dev[id]);

    printf("  [%02d] Throughput      : %9.3f [block/s]\n",
	   id,
	   (double)dev->nio /
	   (TIMEVAL2DOUBLE(iotest.tv[1]) - TIMEVAL2DOUBLE(iotest.tv[0])));
    printf("                       : %9.3f [MB/s]\n",
	   (double)dev->nio * iotest.blksiz /
	   (TIMEVAL2DOUBLE(iotest.tv[1]) - TIMEVAL2DOUBLE(iotest.tv[0]))
	   / MEGA);
    printf("                       : %9.3f [MiB/s]\n",
	   (double)dev->nio * iotest.blksiz /
	   (TIMEVAL2DOUBLE(iotest.tv[1]) - TIMEVAL2DOUBLE(iotest.tv[0]))
	   / MEBI);

    printf("       Avg. Resp. time : %9.3f [ms/block]\n",
	   dev->acciotim * KILO / iotest.nio);
    printf("       Max. Resp. time : %9.3f [ms/block]\n",
	   dev->mxiotim * KILO);
    printf("       Accm. I/O time  : %9.3f [s]\n",
	   dev->acciotim);
}

/*
 * getsize(): returns the size of given file or device in bytes
 */

static unsigned long long getsize(char *fn)
{
    /*
     * Learned from DBKern's subr_getsize.c
     */

    int fd;
    struct stat statbuf;
    unsigned long long size = 0;

    if((fd = open(fn, O_RDONLY, 0)) == -1){
	perror("getsize:open()");
	exit(EXIT_FAILURE);
    }
    
    if(fstat(fd, &statbuf) != 0){
	size = 0;
	goto func_out;
    }

    if(S_ISREG(statbuf.st_mode)){
	size = (unsigned long long)statbuf.st_size;
	goto func_out;
    }

#ifdef __linux__
    if(S_ISBLK(statbuf.st_mode) || S_ISCHR(statbuf.st_mode)){
	int bsiz = 0;
	unsigned long vsiz = 0;

	if(ioctl(fd, BLKSSZGET, &bsiz))
	    perror("getsize:ioctrl()");
	if(ioctl(fd, BLKGETSIZE, &vsiz))
	    perror("getsize:ioctrl()");

	// printf("bsiz=%d\n", bsiz);
	// printf("vsiz=%lu\n", vsiz);
	
	size = (unsigned long long)vsiz*bsiz;
	goto func_out;
    }
#endif

 func_out:
    
    close(fd);
    return(size);
}

/* iotest.c */

