//load input configuration
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <malloc.h>
#include <pthread.h>
#include <limits.h>
#include <signal.h>

#define IDLEN	16

//parameters for each simulated input
struct input {
	char name[IDLEN];	//input name/id
	int prio;		//priority
	
	//all times in msec
	int t0;			//first occurrence, relative to start of simulation
	int T;			//period
	int C;			//needed cpu time for handling one occurrence
	int D;			//deadline, relative to start of period
};

//shared memory is used for communication between controller and input-simulator processes
#define SHM_NAME	"/shm_interface_lab1"

struct input_interface {
	pthread_mutex_t lock;

	//controller's PID and ready status (controller sets them)
	pid_t pid_contr; //controller process ID
	int controller_ready; //0 - not, 1 - yes

	//input simulator's PID, ready status, start/stop command (input simulator sets them)
	pid_t pid_input; //input simulator process ID
	int input_simulator_ready; //0 - not, 1 - yes, input-simulator sets it
	int simulation_start; //0 - not, 1 - yes - for synchronizing start of simulation
	int simulation_stop; //0 - not, 1 - yes - for synchronizing end of simulation

	//for each simulated input
	struct sim_input {
		int request;	//1,2,3,... (0 initially)
		int response;	//same as request after its "handled" (0 initially)
		
		struct timespec t_req;	//timestamp when request is made - input simulator sets it
		struct timespec t_started; //timestamp when request is started being handled - controller sets it
		struct timespec t_resp;	//timestamp when request is handled - controller sets it
	} input[1];
};

int msleep(int msec);
int load_inputs(struct input **_inputs, const char *config);
void init_input_interface(struct input_interface *x, int nr_inputs);
struct input_interface *connect_to_input_interface(int nr_inputs, int create);
void delete_input_interface();

//a few macros for struct timespec
#define TIMESPEC_ADD(A,B) /* A += B */ \
do {                                   \
    (A).tv_sec += (B).tv_sec;          \
    (A).tv_nsec += (B).tv_nsec;        \
    if ( (A).tv_nsec >= 1000000000 ) { \
        (A).tv_sec++;                  \
        (A).tv_nsec -= 1000000000;     \
    }                                  \
} while (0)

#define TIMESPEC_ADD_MS(A,B) /* A += B */    \
do {                                         \
    long nsec = (A).tv_nsec + (B) * 1000000L; \
    (A).tv_sec += nsec / 1000000000;         \
    (A).tv_nsec = nsec % 1000000000;         \
} while (0)

#define TIMESPEC_SUB(A,B) /* A -= B */ \
do {                                   \
    (A).tv_sec -= (B).tv_sec;          \
    (A).tv_nsec -= (B).tv_nsec;        \
    if ( (A).tv_nsec < 0 ) {           \
        (A).tv_sec--;                  \
        (A).tv_nsec += 1000000000;     \
    }                                  \
} while (0)

#define TIMESPEC_TO_MS(A)	((A).tv_sec * 1000 + (A).tv_nsec / 1000000)

#define TIMESPEC_GT(A,B) /* A > B? */ \
( (A).tv_sec > (B).tv_sec || ((A).tv_sec == (B).tv_sec && (A).tv_nsec > (B).tv_nsec) )

#define TIMESPEC_LE(A,B) /* A <= B? */ \
( (A).tv_sec < (B).tv_sec || ((A).tv_sec == (B).tv_sec && (A).tv_nsec <= (B).tv_nsec) )

//printf with time prefix
#define PRINTF(format, ...)                                 \
do {                                                        \
    struct timespec _t_;                                    \
    clock_gettime(CLOCK_MONOTONIC, &_t_);                   \
    TIMESPEC_SUB(_t_, t0);                                  \
    printf("[%03ld:%06ld " WHOAMI "] " format,              \
        _t_.tv_sec % 100, _t_.tv_nsec/1000, ##__VA_ARGS__); \
} while (0)

#if DEBUG==1
#define LOG(format, ...)                                    \
do {                                                        \
    struct timespec _t_;                                    \
    clock_gettime(CLOCK_MONOTONIC, &_t_);                   \
    TIMESPEC_SUB(_t_, t0);                                  \
    printf("[%03ld:%06ld " WHOAMI ":%d] " format,           \
        _t_.tv_sec % 100, _t_.tv_nsec/1000, __LINE__, ##__VA_ARGS__); \
} while (0)
#else
#define LOG(format, ...)
#endif

#define SET_MIN(A,B)   ( (A) = ( (A) < (B) ? (A) : (B) ) )
#define SET_MAX(A,B)   ( (A) = ( (A) > (B) ? (A) : (B) ) )
