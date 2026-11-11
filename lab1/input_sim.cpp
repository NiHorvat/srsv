#include <bits/time.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

#define WHOAMI	"INPUTSIM"

#include "interface.h"
#include "config.h"
//simulated input state
enum input_state {
	INACTIVE, //initially and after being handled
	ACTIVE    //waiting to be handled by controller
};

//run-time data for simulated input
struct input_data {
	enum input_state state; //INACTIVE, ACTIVE
	struct timespec next_run;
	struct timespec next_deadline;
	
	//stats - number of ...
	int nr_wakeups;		//periods in which input could have changed
				//increment at the start of period, regardless if request was handled or not
	int nr_OK_responses;	//periods with responses before deadline
	int nr_LATE_responses;	//periods with late response - both short and long lateness (before next_run and after)
	int nr_NO_responses;	//periods with no response arriving in them

	//stats - times
	//response time: from request till completion
	long response_sum;
	long response_min;
	long response_max;
	
	//reaction time: from request till starting of processing on controller
	long reaction_sum;
	long reaction_min;
	long reaction_max;
};

static struct input *inputs;
static struct input_interface *ii;
static struct input_data *id;
struct timespec t0; //PRINTF


static void sigint_handler(int sig __attribute__((unused)))
{
	PRINTF("SIGINT: ending simulation\n");
	ii->simulation_stop = 1;
}

int main()
{
	clock_gettime(CLOCK_MONOTONIC, &t0);

	int nr_inputs = load_inputs(&inputs, INPUT_FILE);

	delete_input_interface(); //remove existing interface, if exists
	ii = connect_to_input_interface(nr_inputs, 1);
	if (!ii) {
		PRINTF("ERROR: Input simulator failed to create interface\n");
		exit(1);
	}
	ii->pid_input = getpid();
	
	struct sigaction act;
	act.sa_handler = sigint_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);

	id = (struct input_data *) malloc(sizeof(struct input_data) * nr_inputs);

	pthread_mutex_lock(&ii->lock);
	ii->input_simulator_ready = 1; //input simulator ready
	
	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Input simulator ready\n");
	
	//wait for controller to be ready
	if (ii->controller_ready) {
		//controller is ready and waiting for signal
		if (kill(ii->pid_contr, SIGUSR1)) {
			perror("kill error");
			exit(1);
		}
	}
	else {
		pthread_mutex_unlock(&ii->lock);
		//wait for controller to signal us
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
	
	ii->simulation_start = 1;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	PRINTF("Input simulator starting\n");

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	//initialize input data for all inputs based on "inputs[]"
	for (int i = 0; i < nr_inputs; i++) {
		id[i].state = INACTIVE;
		id[i].nr_wakeups = 0;
		id[i].nr_OK_responses = 0;
		id[i].nr_LATE_responses = 0;
		id[i].nr_NO_responses = 0;

		id[i].response_sum = 0;
		id[i].response_min = LONG_MAX;
		id[i].response_max = 0;

		id[i].reaction_sum = 0;
		id[i].reaction_min = LONG_MAX;
		id[i].reaction_max = 0;

		id[i].next_run = now;
		TIMESPEC_ADD_MS(id[i].next_run, inputs[i].t0);
		
		id[i].next_deadline = id[i].next_run;
		TIMESPEC_ADD_MS(id[i].next_deadline, inputs[i].D);
	}

	//main loop
	while (ii->simulation_stop == 0) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		
		//LOG("Checking all inputs\n");

		for (int i = 0; i < nr_inputs && ii->simulation_stop == 0; i++) {
			
			// check the state of an input
			const auto request = ii->input[i].request;
			const auto response = ii->input[i].response;

			//ako je (ulaz "i" nije još aktivan) 
			if(id[i].state == INACTIVE){
				//ako je (je li došlo vrijeme aktivacije?)
				if(TIMESPEC_LE(id[i].next_run, now)){
					/*
					postavi zahtjev na ulaz
					zabilježi trenutak zahtjeva
					označi si da je ulaz sad aktivan
					*/
					
					ii->input[i].t_req = now;
					ii->input[i].request++;
					
					id[i].state = ACTIVE;
					id[i].nr_wakeups++;
					id[i].next_deadline = id[i].next_run;
					
					TIMESPEC_ADD_MS(id[i].next_run, inputs[i].T); // next run pocinje next_run + T
					TIMESPEC_ADD_MS(id[i].next_deadline, inputs[i].D); 
					struct timespec dl = id[i].next_deadline;
					TIMESPEC_SUB(dl, t0); // relative to start, like the PRINTF timestamp
					PRINTF("Activating input %s req %d, deadline at %ld ms\n",
						inputs[i].name, ii->input[i].request, TIMESPEC_TO_MS(dl));


				}
			}
			// id[i].state == ACTIVE
			else{
				//znači da je ulaz aktivan, čekam odgovor
				if(response == request){
					const auto on_D = TIMESPEC_LE(ii->input[i].t_resp, id[i].next_deadline);
					const auto on_T = TIMESPEC_GT(id[i].next_run,ii->input[i].t_resp); // premasilo je sljedeci period

					auto t_response = ii->input[i].t_resp;
					TIMESPEC_SUB(t_response, ii->input[i].t_req);
					const auto t_response_ms = TIMESPEC_TO_MS(t_response);
					

					auto t_reaction = ii->input[i].t_started;

					TIMESPEC_SUB(t_reaction, ii->input[i].t_req);
					const auto t_reaction_ms = TIMESPEC_TO_MS(t_reaction);

					
					id[i].response_sum += t_response_ms;
					SET_MIN(id[i].response_min,t_response_ms);
					SET_MAX(id[i].response_max,t_response_ms);
		

					id[i].reaction_sum += t_reaction_ms;
					SET_MIN(id[i].reaction_min,t_reaction_ms);
					SET_MAX(id[i].reaction_max,t_reaction_ms);
					

					// hack/patch fix
					if(ii->input[i].over_T == true){
						ii->input[i].over_T = false; // reset the over T flag
						id[i].nr_NO_responses++;
						id[i].state = INACTIVE;

						continue;

					}

					if(on_D){
						id[i].nr_OK_responses++;
						PRINTF("Input %s got response on time (%ld ms)\n", inputs[i].name, (long) t_response_ms);
					}
					else if(on_T){
						id[i].nr_LATE_responses++;
						PRINTF("Input %s got late response (%ld ms)\n", inputs[i].name, (long) t_response_ms);
					}
					else{
						PRINTF("Input %s: no response for req %d within period\n",
							inputs[i].name, request);
					}
					
					
					id[i].state = INACTIVE;


				}
				else{
					const auto on_T = TIMESPEC_LE(now, id[i].next_run); 
					PRINTF("Input %s: in the works\n",inputs[i].name);
					if (!on_T) {
						id[i].nr_NO_responses++;
						id[i].nr_wakeups++;
						TIMESPEC_ADD_MS(id[i].next_run, inputs[i].T);   
						PRINTF("Input %s: no response for req %d within period\n",
							inputs[i].name, request);
						// ostajemoi aktivni i dalje cekamo response
						ii->input[i].over_T=true;

					}
				}

			}

			
			
			// 	//TODO
			/*
			
				simulator ulaza
			{
				dok nije označen kraj simulacije {
					za i = 1 do N { //za svaki ulaz
						ako je (ulaz "i" nije još aktivan) {
							ako je (je li došlo vrijeme aktivacije?) {
								postavi zahtjev na ulaz
								zabilježi trenutak zahtjeva
								označi si da je ulaz sad aktivan
							}
						}
						inače {
							//znači da je ulaz aktivan, čekam odgovor
							ako je dobiven odgovor {
								//je li dobiven na vrijeme (prije D)?
								//ažuriraj statistiku
								//pripremi ulaz za iduću pojavu
							}
							inače {
								ako je (odgovor kasni za više od T) {
									povećaj "nema odgovora" u statistici
									//možda još nešto, npr. .iduća_pojava += T
									//ipak čekaj taj odgovor, ne slati novi zahtjev
								}
							}
						}
					}

					odspavaj 1 ms (u simulaciji)
				}

				//na kraju
				izračunaj i ispiši statistiku po svakom ulazu te sumarno za sve
			}
			*/
			
		}
		// tick generator
		pthread_mutex_unlock(&ii->lock);
		msleep(1);
		pthread_mutex_lock(&ii->lock);
	}
	pthread_mutex_unlock(&ii->lock);
	
	int nr_wakeups = 0;
	int nr_OK_responses = 0;
	int nr_LATE_responses = 0;
	int nr_NO_responses = 0;

	long response_sum = 0;
	long response_min = LONG_MAX;
	long response_max = 0;
	
	long reaction_sum = 0;
	long reaction_min = LONG_MAX;
	long reaction_max = 0;
	
	for (int i = 0; i < nr_inputs; i++) {
		//print stat for i-th input
		printf("\nInput %s (t0=%d, T=%d, C=%d, D=%d):\n", inputs[i].name,
			inputs[i].t0, inputs[i].T, inputs[i].C, inputs[i].D);
		printf("nr_wakeups=%d\n", id[i].nr_wakeups);
		printf("nr_OK_responses=%d\n", id[i].nr_OK_responses);
		printf("nr_LATE_responses=%d\n", id[i].nr_LATE_responses);
		printf("nr_NO_responses=%d\n", id[i].nr_NO_responses);

		printf("response_avg=%ld\n", id[i].response_sum/(id[i].nr_OK_responses + id[i].nr_LATE_responses));
		printf("response_min=%ld\n", id[i].response_min);
		printf("response_max=%ld\n", id[i].response_max);

		printf("reaction_avg=%ld\n", id[i].reaction_sum/(id[i].nr_OK_responses + id[i].nr_LATE_responses));
		printf("reaction_min=%ld\n", id[i].reaction_min);
		printf("reaction_max=%ld\n", id[i].reaction_max);

		nr_wakeups += id[i].nr_wakeups;
		nr_OK_responses += id[i].nr_OK_responses;
		nr_LATE_responses += id[i].nr_LATE_responses;
		nr_NO_responses += id[i].nr_NO_responses;

		response_sum += id[i].response_sum;
		SET_MIN(response_min, id[i].response_min);
		SET_MAX(response_max, id[i].response_max);

		reaction_sum += id[i].reaction_sum;
		SET_MIN(reaction_min, id[i].reaction_min);
		SET_MAX(reaction_max, id[i].reaction_max);
	}

	printf("\nOverall stats:\n");
	printf("nr_wakeups=%d\n", nr_wakeups);
	printf("nr_OK_responses=%d\n", nr_OK_responses);
	printf("nr_LATE_responses=%d\n", nr_LATE_responses);
	printf("nr_NO_responses=%d\n", nr_NO_responses);

	printf("response_avg=%ld\n", response_sum/(nr_OK_responses + nr_LATE_responses));
	printf("response_min=%ld\n", response_min);
	printf("response_max=%ld\n", response_max);

	printf("reaction_avg=%ld\n", reaction_sum/(nr_OK_responses + nr_LATE_responses));
	printf("reaction_min=%ld\n", reaction_min);
	printf("reaction_max=%ld\n", reaction_max);

	return 0;
}
