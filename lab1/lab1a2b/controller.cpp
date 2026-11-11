#include <ctime>
#include <stdio.h>

#define WHOAMI	"CONTROLLER"

#include "interface.h"
#include "config.h"
#include <thread>
#include <iostream>
#include <functional>


static struct input *inputs;
static struct input_interface *ii;
struct timespec t0; //for PRINTF & LOG
int nr_inputs;
static void sigint_handler(int sig __attribute__((unused)))
{
	PRINTF("SIGINT: ending simulation\n");
	ii->simulation_stop = 1;
}



void thread_task_internal(bool pair){

	//thread loop
	while (ii->simulation_stop == 0) {
		int inputs_handled = 0;
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);

		for (int i = 0; i < nr_inputs && ii->simulation_stop == 0; i++) {			
			if(i%2 != pair) continue;
			/**
			upravljač
				{
					dok nije označen kraj simulacije {
						za i=0 do br_ulaza-1 {
							ako je (promjena na ulazu "i") {
								zabilježi trenutak reakcije
								simuliraj obradu ulaza "i" (sleep)
								označi da je obrada gotova
							}
						}
					}
				}
							
			
			*/
			// svaki request simulator radi request++
			const auto last_request =  ii->input[i].request;
			const auto last_response = ii->input[i].response;
			if (last_request != last_response) {
				clock_gettime(CLOCK_MONOTONIC, &now);
				ii->input[i].t_started = now;
				PRINTF("thread%d : START processing input %s request %d (C=%d ms)\n",
					pair,inputs[i].name, last_request, inputs[i].C);

				msleep(inputs[i].C);

				clock_gettime(CLOCK_MONOTONIC, &now);
				ii->input[i].t_resp = now;
				ii->input[i].response = last_request;
				PRINTF("END processing input %s request %d\n", inputs[i].name, last_request);
				inputs_handled++;
			}

		}
	
	}


}

auto thread_task1 = std::bind(thread_task_internal, true);
auto thread_task2 = std::bind(thread_task_internal, false);



int main()
{
	clock_gettime(CLOCK_MONOTONIC, &t0);

	nr_inputs = load_inputs(&inputs, INPUT_FILE);

	ii = connect_to_input_interface(nr_inputs, 0);
	if (!ii) {
		PRINTF("ERROR: Controller failed to connect to interface! Is input-simulator started?\n");
		exit(1);
	}
	ii->pid_contr = getpid();

	struct sigaction act;
	act.sa_handler = sigint_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);

	pthread_mutex_lock(&ii->lock);
	ii->controller_ready = 1; //input simulator ready
	
	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Controller ready\n");
	
	//wait for input simulator to be ready
	if (ii->input_simulator_ready) {
		//input simulator is ready and waiting for signal
		if (kill(ii->pid_input, SIGUSR1)) {
			perror("kill error");
			exit(1);
		}
	}
	else {
		pthread_mutex_unlock(&ii->lock);
		//wait for input simulator to signal us
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGUSR1);
		if (pthread_sigmask(SIG_BLOCK, &set, NULL)) {
			perror("pthread_sigmask error");
			exit(1);
		}
		int sig;
		if (sigwait(&set, &sig)) {
			perror("sigwait error");
			exit(1);
		}
		pthread_mutex_lock(&ii->lock);
	}
	
	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Controller starting\n");
	pthread_mutex_unlock(&ii->lock);

	std::thread t1(thread_task1);
	std::thread t2(thread_task2);



	// Join t1 if joinable
    if (t1.joinable()) {
        t1.join();
        std::cout << "t1 joined\n";
    }

    // Detach t2 if joinable
    if (t2.joinable()) {
        t2.detach();
        std::cout << "t2 detached\n";
    }

	PRINTF("Controller ending\n");
	pthread_mutex_unlock(&ii->lock);

	return 0;
}
