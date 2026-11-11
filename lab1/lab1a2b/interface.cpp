//load input configuration
#include "interface.h"
#include "config.h"

// sleep in milliseconds
int msleep(int msec)
{
	struct timespec t;
	t.tv_sec = msec / 1000;
	t.tv_nsec = (msec % 1000) * 1000000;
	return clock_nanosleep(CLOCK_MONOTONIC, 0, &t, NULL);
}

//load inputs from configuration file
//NOTE: not tested/not working correctly on bad formatted files
int load_inputs(struct input **_inputs, const char *config)
{
	FILE *fp = fopen(config, "r");
	if (!fp) {
		fprintf(stderr, "Error opening config file %s:", config);
		perror(NULL);
		return -1;
	}
	struct input *inputs = NULL;
	int count = 0;
	while (!feof(fp)) {
		char line[128];
		if (fgets(line, 128, fp) && line[0] != '#' && line[0] != '\n') {
			struct input inp;
			sscanf(line, "%s %d %d %d %d %d", inp.name, &inp.prio, &inp.t0, &inp.T, &inp.C, &inp.D); 
			//printf("%s %d %d %d %d %d\n", inp.name, inp.prio, inp.t0, inp.T, inp.C, inp.D);

			count++;
			struct input *tmp = (struct input *) realloc(inputs, count * sizeof(struct input));
			if (!tmp) {
				free(inputs);
				perror("malloc error:");
				return -1;
			}
			inputs = tmp;
			inputs[count-1] = inp;
		}
	}
	*_inputs = inputs;
	
	return count;
}

//initialize input interface (assuming its already created!)
void init_input_interface(struct input_interface *x, int nr_inputs)
{
	//x->lock is used between threads of different processes
	pthread_mutexattr_t attr;
	if (pthread_mutexattr_init(&attr) ||
		pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) ||
		pthread_mutex_init(&x->lock, &attr)
	) {
		perror("mutex init");
		exit(1);
	}
	
	x->controller_ready = 0;
	x->input_simulator_ready = 0;
	x->simulation_start = 0;
	x->simulation_stop = 0;

	for (int i = 0; i < nr_inputs; i++) {
		x->input[i].request = 0;
		x->input[i].response = 0;
		x->input[i].t_req = (struct timespec) {0, 0};
		x->input[i].t_resp = (struct timespec) {0, 0};
		x->input[i].t_started = (struct timespec) {0, 0};
	}
}

//connect or create shared memory object as interface between controller and input simulator
struct input_interface *connect_to_input_interface(int nr_inputs, int create)
{
	int id;
	struct input_interface *x;
	size_t shm_size = sizeof(struct input_interface) + (nr_inputs - 1) * sizeof(input_interface::sim_input);
	int created = 0;
	
	//exist?
	id = shm_open(SHM_NAME, O_RDWR, 00600);
	if (id == -1) {
		if (create) {
			//doesn't exist, try again with O_CREAT flag
			id = shm_open(SHM_NAME, O_CREAT | O_RDWR, 00600);
			if (id == -1 || ftruncate(id, shm_size) == -1) {
				perror("shm_open/ftruncate");
				exit(1);
			}
			created = 1;
		}
		else {
			return NULL;
		}
	}
	x = (struct input_interface *) mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, id, 0);
	if (x == (void *) -1) {
		perror("mmap");
		exit(1);
	}
	close(id);
	
	if (created) 
		init_input_interface(x, nr_inputs);

	return x;
}

void delete_input_interface()
{
	shm_unlink(SHM_NAME);
}
