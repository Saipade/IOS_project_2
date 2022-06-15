#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <time.h>  
#include <sys/stat.h>       
#include <semaphore.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>

#define mmap_var(ptr) {(ptr) = mmap(NULL, sizeof(*(ptr)), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);}
#define mmap_arr(ptr, mult) {(ptr) = mmap(NULL, sizeof(*(ptr))*(mult), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);}

int *LineNumber = NULL; // line number
int *ELVES_IN_QUEUE = NULL; // number of elves in queue
int *ELVES_IN_QUEUE1 = NULL; // internal counter of elves in queue; for proper number of "GO_HELP" tokens given to Santa
int *REINDEERS_ACTIVE = NULL; // number of reindeers returned to workshop
int *REINDEERS_HITCHED = NULL; // number of hitched reindeerss
int *ELVES_QUEUE = NULL; // array of elfIDs of elves that are in queue
char **ELF_STATUS = NULL; // array of current elf[elfID] states
char **REINDEER_STATUS = NULL; // array of current RD[rdID] states
char **SANTA_STATUS = NULL; // current Santa's state
FILE *pfile;

void print_santa_status(sem_t *semaphore) {
    sem_wait(semaphore);
    pfile = fopen("proj2.out", "a");
    fprintf(pfile, "%d: Santa: %s\n", ++LineNumber[0], *SANTA_STATUS);
    fclose(pfile);
    sem_post(semaphore);
}

void print_rd_status(sem_t *semaphore, int id) {
    sem_wait(semaphore);
    pfile = fopen("proj2.out", "a");
    fprintf(pfile, "%d: RD %d: %s\n", ++LineNumber[0], id, REINDEER_STATUS[id]);
    fclose(pfile);
    sem_post(semaphore);
}

void print_elf_status(sem_t *semaphore, int id) {
    sem_wait(semaphore);
    pfile = fopen("proj2.out", "a");
    fprintf(pfile, "%d: Elf %d: %s\n", ++LineNumber[0], id, ELF_STATUS[id]);
    fclose(pfile);
    sem_post(semaphore);
}

int main (int argc, char **argv) {
    
    // If number of arguments is not 5 (./proj2 + 4 arguments) -> return 1
    if (argc != 5) {
        printf("Error. Number of arguments is not 4.\nCorrect format is: ./proj2 NE NR TE TR\nWhere 0 < NE < 1000, 0 < NR < 20, 0 <= TE <= 1000, 0 <= TR <= 1000\n"); 
        return 1;
    }

    // If there are not only integers in argv -> return 1
    
    for (int i = 1; i < argc; i++) { 
        int length = strlen(argv[i]);
        for (int j = 0; j < length; j++) {
            if (argv[i][j] > '9' || argv[i][j] < '0') {
                printf("Error. Invalid input format.\n");
                return 1;
            }
        }
    }

    int N_ELVES     = atoi(argv[1]); // 0 < NE < 1000
    int N_REINDEERS = atoi(argv[2]); // 0 < NR < 20
    int T_ELVES     = atoi(argv[3]); // 0 <= TE <= 1000
    int T_REINDEERS = atoi(argv[4]); // 0 <= TR <= 1000

    // If every previous parameters meet the conditions
    if (N_ELVES >= 1000 || N_ELVES <= 0 || N_REINDEERS >= 20 || N_REINDEERS <= 0
    || T_ELVES < 0 || T_ELVES > 1000 || T_REINDEERS < 0 || T_REINDEERS > 1000) {
        printf("Error. Invalid input values.\n");
        return 1;
    }
    
    // initialization
    pfile = fopen("proj2.out", "w");
    fclose(pfile);

    // variables declaration
    useconds_t* ELVES_WAITING_TIME = calloc(N_ELVES, sizeof(int)); // array for random elves' waiting time
    useconds_t* REINDEERS_WAITING_TIME = calloc(N_REINDEERS, sizeof(int)); // array for random RDs' waiting time

    // shared variables memory mapping
    mmap_var(LineNumber);
    mmap_var(ELVES_IN_QUEUE);
    mmap_var(ELVES_IN_QUEUE1);
    mmap_var(REINDEERS_ACTIVE);
    mmap_var(SANTA_STATUS);
    mmap_arr(ELVES_QUEUE, N_ELVES);
    mmap_arr(ELF_STATUS, N_ELVES);
    mmap_arr(REINDEER_STATUS, N_REINDEERS);

    // semaphores declaration and initialization
    sem_t *RD_NUMBER_INC = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    sem_t *ELF_NUMBER_INC = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    sem_t *WRITING_OUTPUT = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    sem_t *ALL_HITCHED = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    sem_t *GO_HELP = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    sem_t *printed = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    sem_t *closed = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
    sem_t *HITCHED[N_REINDEERS];
    sem_t *HELPED[N_ELVES];
    for (int i = 1; i <= N_REINDEERS; i++) {
        HITCHED[i] = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
        sem_init(HITCHED[i], 1, 0);
    }
    for (int i = 1; i <= N_ELVES; i++) {
        HELPED[i] = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,0);
        sem_init(HELPED[i], 1, 0);
    }
    sem_init(WRITING_OUTPUT, 1, 1);
    sem_init(RD_NUMBER_INC, 1, 1);
    sem_init(ELF_NUMBER_INC, 1, 1);
    sem_init(ALL_HITCHED, 1, 0);
    sem_init(GO_HELP, 1, 0);
    sem_init(printed, 1, 3);
    sem_init(closed, 1, 0);

    // reindeers, elves and santa processes definition
    int santa = fork();
    int elf_or_reindeer = fork();
    if (santa == 0) {
        if (elf_or_reindeer == 0) {
            for (int rdID = 1; rdID <= N_REINDEERS; rdID++) {
                if (fork() == 0) { // reindeers
                    // waiting time generation for every reindeer
                    srand(time(NULL) ^ getpid()); 
                    REINDEERS_WAITING_TIME[rdID] = ((rand() % (T_REINDEERS - T_REINDEERS/2 + 1)) + T_REINDEERS/2) * 1000;
                    
                    REINDEER_STATUS[rdID] = "rstarted";
                    print_rd_status(WRITING_OUTPUT, rdID);

                    usleep(REINDEERS_WAITING_TIME[rdID]);

                    REINDEER_STATUS[rdID] = "return home";
                    print_rd_status(WRITING_OUTPUT, rdID);

                    sem_wait(RD_NUMBER_INC);
                    REINDEERS_ACTIVE[0]++;
                    sem_post(RD_NUMBER_INC);
                    // waiting for hitching
                    sem_wait(HITCHED[rdID]); 

                    sem_wait(RD_NUMBER_INC);
                    REINDEER_STATUS[rdID] = "get hitched";
                    print_rd_status(WRITING_OUTPUT, rdID);
                    REINDEERS_ACTIVE[0]++;
                    sem_post(RD_NUMBER_INC);
                    // if every reindeer is hitched
                    if (REINDEERS_ACTIVE[0] == N_REINDEERS) {
                        sem_post(ALL_HITCHED);
                    }
                    
                    exit(0);
                }
            }
            exit(0);
        } else if (elf_or_reindeer < 0) {
            printf("Error\n");
            return 1;
        } else {
            for (int elfID = 1; elfID <= N_ELVES; elfID++) {
                if (fork() == 0) { // elves
                    // waiting time generation for every elf
                    srand(time(NULL) ^ getpid());
                    ELVES_WAITING_TIME[elfID] = (rand() % (T_ELVES + 1)) * 1000;

                    ELF_STATUS[elfID] = "started";
                    print_elf_status(WRITING_OUTPUT, elfID);
                    
                    while (true) {

                        usleep(ELVES_WAITING_TIME[elfID]);

                        ELF_STATUS[elfID] = "need help";

                        sem_wait(ELF_NUMBER_INC); 
                        print_elf_status(WRITING_OUTPUT, elfID);

                        if (strcmp(SANTA_STATUS[0], "closing workshop") == 0 || strcmp(SANTA_STATUS[0], "Christmas started") == 0) {
                            ELF_STATUS[elfID] = "taking holidays";
                            sem_post(ELF_NUMBER_INC);     
                            break;    
                        }

                        ELVES_QUEUE[ELVES_IN_QUEUE[0]] = elfID; // elves to queue
                        ELVES_IN_QUEUE[0]++;
                        ELVES_IN_QUEUE1[0]++;
                        sem_post(ELF_NUMBER_INC);

                        sem_wait(ELF_NUMBER_INC); // tokens to Santa. Number of tokens = number of elves in queue DIV 3
                        if (ELVES_IN_QUEUE1[0] >= 3) {
                            sem_post(GO_HELP);
                            ELVES_IN_QUEUE1[0]-=3;
                        }
                        sem_post(ELF_NUMBER_INC);

                        sem_wait(HELPED[elfID]); // waiting for help from Santa

                        if (strcmp(SANTA_STATUS[0], "closing workshop") == 0 || strcmp(SANTA_STATUS[0], "Christmas started") == 0) {                          ELF_STATUS[elfID] = "taking holidays";     
                            break;    
                        }
                        
                        sem_wait(ELF_NUMBER_INC); // shifting queue array and decreasing in-queue counter
                        for (int e = 0; e < ELVES_IN_QUEUE[0] - 1; e++) {
                            ELVES_QUEUE[e] = ELVES_QUEUE[e + 1];
                        }  
                        ELVES_IN_QUEUE[0]--;
                        sem_post(ELF_NUMBER_INC);

                        if (strcmp(SANTA_STATUS[0], "closing workshop") == 0 || strcmp(SANTA_STATUS[0], "Christmas started") == 0) {
                            ELF_STATUS[elfID] = "taking holidays";     
                            break;    
                        } else {
                            ELF_STATUS[elfID] = "get help";
                        }
                        
                        
                        print_elf_status(WRITING_OUTPUT, elfID);
                        sem_post(printed);

                    }
                    sem_wait(closed);
                    print_elf_status(WRITING_OUTPUT, elfID);

                    exit(0);
                }
            }
            exit(0);
        }
    
    } else if (santa < 0) {
        printf("Error\n");
        return 1;
    } else {
        if (elf_or_reindeer == 0) { // Santa

            if (N_ELVES >= 3) {
                while (true) {
                    SANTA_STATUS[0] = "going to sleep";
                    sem_wait(printed);
                    sem_wait(printed);
                    sem_wait(printed);
                    print_santa_status(WRITING_OUTPUT);
                    
                    if (REINDEERS_ACTIVE[0] == N_REINDEERS) {
                        break;
                    }

                    sem_wait(GO_HELP);
                    SANTA_STATUS[0] = "helping elves";
                    print_santa_status(WRITING_OUTPUT);
                    for (int s = 0; s < 3; s++) {
                        sem_post(HELPED[ELVES_QUEUE[s]]);
                    }
                }

            } else {

                SANTA_STATUS[0] = "going to sleep";
                print_santa_status(WRITING_OUTPUT);

                while (true) {
                    if (REINDEERS_ACTIVE[0] == N_REINDEERS) {
                        break;
                    }
                }
            }

            REINDEERS_ACTIVE[0] = 0;
            SANTA_STATUS[0] = "closing workshop";
            print_santa_status(WRITING_OUTPUT);
            
            for (int s = 0; s <= N_ELVES; s++) {
                sem_post(closed);
            }
            for (int s = 1; s <= N_REINDEERS; s++) {
                sem_post(HITCHED[s]);
            }
            sem_wait(ALL_HITCHED);
            SANTA_STATUS[0] = "Christmas started";
            print_santa_status(WRITING_OUTPUT);

            exit(0);
            
        } else if (elf_or_reindeer < 0) {
            printf("Error\n");
            return 1;
        } else { 
            while (true) { // elves' hivemind
                sem_wait(closed);
                for (int o = 1; o <= N_ELVES; o++) {
                    sem_post(HELPED[o]);
                }
                break;
            }
        }
    }

    free(ELVES_WAITING_TIME);
    free(REINDEERS_WAITING_TIME);

    munmap(LineNumber, sizeof(LineNumber));
    munmap(ELVES_IN_QUEUE, sizeof(ELVES_IN_QUEUE));
    munmap(ELVES_IN_QUEUE1, sizeof(ELVES_IN_QUEUE1));
    munmap(REINDEERS_ACTIVE, sizeof(REINDEERS_ACTIVE));
    munmap(ELF_STATUS, sizeof(ELF_STATUS)*N_ELVES);
    munmap(REINDEER_STATUS, sizeof(REINDEER_STATUS)*N_REINDEERS);
    munmap(SANTA_STATUS, sizeof(SANTA_STATUS));
    munmap(ELVES_QUEUE, sizeof(ELVES_QUEUE)*N_ELVES);

    munmap(WRITING_OUTPUT, sizeof(WRITING_OUTPUT));
    munmap(ELF_NUMBER_INC, sizeof(ELF_NUMBER_INC));
    munmap(RD_NUMBER_INC, sizeof(RD_NUMBER_INC));
    munmap(ALL_HITCHED, sizeof(ALL_HITCHED));
    munmap(GO_HELP, sizeof(GO_HELP));
    munmap(printed, sizeof(printed));
    munmap(closed, sizeof(closed));
    sem_close(WRITING_OUTPUT);
    sem_close(ELF_NUMBER_INC);
    sem_close(RD_NUMBER_INC);
    sem_close(ALL_HITCHED);
    sem_close(GO_HELP);
    sem_close(printed);
    sem_close(closed);
    sem_destroy(WRITING_OUTPUT);
    sem_destroy(ELF_NUMBER_INC);
    sem_destroy(RD_NUMBER_INC);
    sem_destroy(ALL_HITCHED);
    sem_destroy(GO_HELP);
    sem_destroy(printed);
    sem_destroy(closed);
    for (int i = 1; i <= N_REINDEERS; i++) {
        munmap(HITCHED[i], sizeof(HITCHED[i]));
        sem_close(HITCHED[i]);
        sem_destroy(HITCHED[i]);
    }
    for (int i = 1; i <= N_ELVES; i++) {
        munmap(HELPED[i], sizeof(HELPED[i]));
        sem_close(HELPED[i]);
        sem_destroy(HELPED[i]);
    }
    return 0;
}