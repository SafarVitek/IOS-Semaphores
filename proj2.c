/****************************************************************
 * Název projektu: Projekt 2 - H2O
 * Soubor: proj2.c
 * Datum: 15.4.2022
 * Poslední změna: 22.4.2022
 * Autor: Vítězslav Šafář
 *
 * Popis: Zpracování atomů vodíků a kyslíku do molekuly vody
 ****************************************************************/
/**
 * @file proj2.c
 * @brief Zpracování atomů vodíků a kyslíku do molekuly vody
 *
 * @author Vítězslav Šafář
 */

#include <stdio.h>
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <memory.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>

/**
 * Struktura pro vstupní argumenty (configuraci programu)
 */
typedef struct config {
    unsigned int oxygen_amount;     //vstupní počet kyslíků
    unsigned int hydrogen_amount;   //vstupní počet vodíků
    unsigned int wait_time;         //max doba čekání atomů
    unsigned int bond_time;         //max doba vytváření molekuly
}config_t;

/**
 * Struktura pro sdílenou paměť mezi procesy
 */
typedef struct shared_memory{
    int action_id;                  //ID akce (A ze zadání)
    int oxygen_id;                  //ID kyslíku
    int hydrogen_id;                //ID vodíku
    int molecule_id;                //ID molekuly
    int oxygens;                    //aktuální počet načtených atomů kyslíků
    int hydrogens;                  //aktuální počet naštených atomů vodíků
    int bond;                       //počitadlo molekul vstupujících do bond funkce
    int shutdown;                   //proměná pro signalizaci vytvoření všech molekul a ukončení zbylých procesů
    int max_molecules;              //max počet molekul, které je možno vytvořit
}shared_memory_t;

/**
 * Struktura semaforů
 */
typedef struct semaphores{
    sem_t *mutex;                   //semafor na synchronizaci procesu kyslíku s procesem vodíku
    sem_t *oxyQueue;                //fronta atomů kyslíků
    sem_t *hydroQueue;              //fronta atomů vodíků
    sem_t *barrier;                 //bariera, která pouští vytvořené molekuly
    sem_t *print_mutex;             //semafor pro synchronizaci zápisů do souboru
    sem_t *bond_mutex;              //semafor pro synchronizaci funkce bond
    sem_t *mol_created;             //semafor pro signalizaci ukončení doby čekání při vytvoření molekuly
    sem_t *release_function;        //semafor pro ukončení přebytečných procesů
}semaphores_t;

/**
 * Funkce pro validaci vstupních argumentů a následné uložení do struktury config
 * @param config Struktura vstupních argumentů
 * @param argc Počet argumentů
 * @param argv Pole argumentů
 * @return Vrací 0 pokud jsou všechny argumenty v pořádku, Vrací -1 až -5 podle toho, který argument je chybně
 */
int valid_config(config_t *config, int argc, char **argv);

/**
 * Funkce na alokaci sdílené paměti a nastavení základních hodnot
 * @return Vrací alokovanou paměť nebo NULL pokud nastala chyba
 */
shared_memory_t *init_shared_memory(config_t *config);

/**
 * Funckce pro inicializaci(otevření) všech semaforů
 * @param semaphores Struktura semaforů
 * @return Vrací 0 pokud se všechny semafory otevřely, Vrací -1 pokud nastala chyba
 */
int open_semaphores(semaphores_t *semaphores);

/**
 * Funkce pro zavření všech semaforů
 * @param semaphores Struktura semaforů
 */
void close_semaphores(semaphores_t *semaphores);

/**
 * Funkce pro uvolnění všech semaforů
 */
void unlink_semaphores();

/**
 * Funkce pro vypsání textu do souboru
 * @param file Výstupní soubor
 * @param shared_memory Sdílená paměť
 * @param semaphores Struktura semaforů
 * @param msg Text na vytisknutí
 * @param ... Dodatečné argumenty
 */
void print_to_file(FILE *file, shared_memory_t *shared_memory, semaphores_t *semaphores, char *msg, ...);

/**
 * Funkce pro proces kyslík
 * @param file Výstupní soubor
 * @param shared_memory Sdílená paměť
 * @param semaphores Struktura semaforů
 * @param config Struktura vstupních argumentů
 */
void oxygen_function(FILE *file, shared_memory_t *shared_memory, semaphores_t *semaphores, config_t *config);

/**
 * Funkce pro proces vodík
 * @param file Výstupní soubor
 * @param shared_memory Sdílená paměť
 * @param semaphores Struktura semaforů
 * @param config Struktura vstupních argumentů
 */
void hydrogen_function(FILE *file, shared_memory_t *shared_memory, semaphores_t *semaphores, config_t *config);

/**
 * Funkce na spojení atomů kyslíku a vodíků v molekulu
 * @param shared_memory Sdílená paměť
 * @param semaphores Struktura semaforů
 */
void bond(shared_memory_t *shared_memory, semaphores_t *semaphores);

/**
 * Funkce na ukončení přebytečných procesů vodíku a kyslíku, které se nesloučí v molekulu vody
 * @param shared_memory Sdílená paměť
 * @param semaphores Struktura semaforů
 * @param config Struktura vstupních argumentů
 */
void shutdown_processes(shared_memory_t *shared_memory, semaphores_t *semaphores, config_t *config);

/**
 * Funcke na získání náhodného čísla menší jak vstupní hodnota
 * @param value Maximální hodnota
 * @return Vrátí náhodné číslo
 */
unsigned int random_number(unsigned int value);

//argv[1] = počet kyslíků
//argv[2] = počet vodíků
//argv[3] = doba čekání atomu po vytvoření
//argv[4] = doba vytvoření
int main(int argc, char *argv[]) {
    config_t config;
    int valid = valid_config(&config, argc, argv);
    //!Výpis chyb v argumentech
    switch (valid) {
        case -1:
            fprintf(stderr, "Počet kyslíků je ve špatném formátu!!!\n");
            return 1;
        case -2:
            fprintf(stderr, "Počet vodíků je ve špatném formátu!!!\n");
            return 1;
        case -3:
            fprintf(stderr, "Doba čekání je ve špatném formátu!!!\n");
            return 1;
        case -4:
            fprintf(stderr, "Doba vytvoření je ve špatném formátu!!!\n");
            return 1;
        case -5:
            fprintf(stderr, "Špatný počet argumentů!!!\n");
            return 1;
        default:
            break;
    }

    //!Otevření/vytvoření výstupního souboru
    FILE *file = fopen("proj2.out", "w");
    if (file == NULL) {                                 // Nepodařilo se otevřít/vytvořit soubor
        fprintf(stderr, "Soubor proj2.out nelze otevřít!!!\n");
        return 1;
    }
    //!Vytvoření sdílené paměti
    shared_memory_t *shared_memory = init_shared_memory(&config);
    if (shared_memory == NULL) {                        // Nepodařilo se alokovat paměť
        fprintf(stderr, "Chyba alokování sdílené paměti!!!\n");
        fclose(file);
        return 1;
    }
    //!Vytvoření struktury semaforů
    semaphores_t semaphores = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    if (open_semaphores(&semaphores) == -1) {           // Nepodařilo se otevřít jeden ze semaforů
        fprintf(stderr, "Chyba otevření semaforů :%s\n", strerror(errno));
        fclose(file);

        munmap(shared_memory, sizeof(shared_memory_t)); // Uvolnění sdílené paměti
        unlink_semaphores();                            // Uvolnění semaforů
        close_semaphores(&semaphores);                  // Zavření semaforů
        return 1;
    }
    //!Vytváření procesů
    pid_t processes[config.oxygen_amount + config.hydrogen_amount];
    for (unsigned int i = 0; i < (config.oxygen_amount + config.hydrogen_amount); ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            if (i < config.oxygen_amount) {
                //!Vytvoření procesu kyslíku
                srand(time(NULL) ^ (getpid()<<16));
                oxygen_function(file, shared_memory, &semaphores, &config);
            } else {
                //!Vytvoření procesu vodíku
                srand(time(NULL) ^ (getpid() << 16));
                hydrogen_function(file, shared_memory, &semaphores, &config);
            }
            fclose(file);                               // Zavření souboru pro dětské procesy
            close_semaphores(&semaphores);
            return 0;
        } else if (pid == -1) {                         // Chyba forku
            fprintf(stderr, "Fork failed :%s\n", strerror(errno));
            fclose(file);

            munmap(shared_memory, sizeof(shared_memory_t)); // Uvolnění sdílené paměti

            unlink_semaphores();                        // Uvolnění všech semaforů
            close_semaphores(&semaphores);              // Uzavření všech semaforů
            for (unsigned int j = 0; j < i; ++j) {
                kill(processes[j], SIGKILL);            // Ukončení již spuštěných procesů
            }
            return 1;
        }
    }
    fclose(file);                                       // Zavření souboru pro main proces

    if(shared_memory->max_molecules != 0){              // V případě vytvoření 0 molekul se nemusí čekat na jejich vytvoření
        sem_wait(semaphores.release_function);          // Semafor se uvolní až budou všechny molekuly vytvořeny
    }



    shutdown_processes(shared_memory, &semaphores, &config);    // Volání funkce pro vypuštění přebytečných atomů
    //!Čekání na ukončení všech procesů
    for (unsigned i = 0; i < config.oxygen_amount + config.hydrogen_amount; i++) {
        wait(&processes[i]);
    }

    unlink_semaphores(); // Uvolnění všech semaforů
    close_semaphores(&semaphores); // Uzavření všech semaforů
    munmap(shared_memory, sizeof(shared_memory_t)); // Uvolnění sdílené paměti

    return 0;
}

int valid_config(config_t *config, int argc, char **argv){
    if(argc != 5){
        return -5;
    }
    char *end;
    long args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = strtol(argv[i + 1], &end, 10);
        if (*end != '\0') {                             // Argument není číslo
            return -(i+1);
        }
        if (args[i] < 0) {                              //Argument není kladné číslo
            return -(i+1);
        }
    }
    //! Specifické podmínky pro každý z argumentů
    if(args[0] == 0){
        return -1;
    }
    if(args[1] == 0){
        return -2;
    }
    if(args[2] > 1000){
        return -3;
    }
    if(args[3] > 1000){
        return -4;
    }
    //!Uložení argumentů do struktury
    config->oxygen_amount = args[0];
    config->hydrogen_amount = args[1];
    config->wait_time = args[2];
    config->bond_time = args[3];
    return 0;
}

shared_memory_t *init_shared_memory(config_t *config){
    //!Alokování paměti
    shared_memory_t *shared_memory = mmap(NULL, sizeof(shared_memory_t), PROT_READ | PROT_WRITE,
                                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_memory == NULL) {
        return NULL;
    }
    //!Nastavení základních hodnot sdílené paměti
    shared_memory->action_id = 1;
    shared_memory->oxygen_id = 1;
    shared_memory->hydrogen_id = 1;
    shared_memory->molecule_id = 0;
    shared_memory->oxygens = 0;
    shared_memory->hydrogens = 0;
    shared_memory->bond = 0;
    shared_memory->shutdown = 0;
    //! Spočtení počtu molekul, které můžeme vytvořit s daným počtem kyslíků a vodíků
    if(config->oxygen_amount > config->hydrogen_amount/2){
        shared_memory->max_molecules = (int)config->hydrogen_amount/2;
    }else{
        shared_memory->max_molecules = (int)config->oxygen_amount;
    }
    return shared_memory;
}

int open_semaphores(semaphores_t *semaphores) {
    semaphores->oxyQueue = sem_open("/oxyQueue", O_CREAT | O_EXCL, 0666, 0);
    if (semaphores->oxyQueue == SEM_FAILED) {
        return -1;
    }
    semaphores->hydroQueue = sem_open("/hydroQueue", O_CREAT | O_EXCL, 0666, 0);
    if (semaphores->hydroQueue == SEM_FAILED) {
        return -1;
    }
    semaphores->barrier = sem_open("/barrier", O_CREAT | O_EXCL, 0666, 0);
    if (semaphores->barrier == SEM_FAILED) {
        return -1;
    }
    semaphores->mutex = sem_open("/mutex", O_CREAT | O_EXCL, 0666, 1);
    if (semaphores->mutex == SEM_FAILED) {
        return -1;
    }
    semaphores->print_mutex = sem_open("/print_mutex", O_CREAT | O_EXCL, 0666, 1);
    if (semaphores->print_mutex == SEM_FAILED) {
        return -1;
    }
    semaphores->bond_mutex = sem_open("/bond_mutex", O_CREAT | O_EXCL, 0666, 1);
    if (semaphores->bond_mutex == SEM_FAILED) {
        return -1;
    }
    semaphores->mol_created = sem_open("/mol_created", O_CREAT | O_EXCL, 0666, 0);
    if (semaphores->mol_created == SEM_FAILED) {
        return -1;
    }
    semaphores->release_function = sem_open("/release_function", O_CREAT | O_EXCL, 0666, 0);
    if (semaphores->release_function == SEM_FAILED) {
        return -1;
    }
    return 0;
}
void close_semaphores(semaphores_t *semaphores){
    sem_close(semaphores->oxyQueue);
    sem_close(semaphores->hydroQueue);
    sem_close(semaphores->mutex);
    sem_close(semaphores->barrier);
    sem_close(semaphores->print_mutex);
    sem_close(semaphores->bond_mutex);
    sem_close(semaphores->mol_created);
    sem_close(semaphores->release_function);
}

void unlink_semaphores(){
    sem_unlink("/oxyQueue");
    sem_unlink("/hydroQueue");
    sem_unlink("/mutex");
    sem_unlink("/barrier");
    sem_unlink("/print_mutex");
    sem_unlink("/bond_mutex");
    sem_unlink("/mol_created");
    sem_unlink("/release_function");
}

void print_to_file(FILE *file, shared_memory_t *shared_memory, semaphores_t *semaphores, char *msg, ...){
    va_list arg_list;
    va_start(arg_list, msg);

    sem_wait(semaphores->print_mutex);                  // Mutex, aby do souboru mohl vždy zapisovat pouze jeden proces
    fprintf(file, "%d: ", shared_memory->action_id++);
    vfprintf(file, msg, arg_list);
    fflush(file);                                       // Zahození mezipaměti souboru
    sem_post(semaphores->print_mutex);
}
void oxygen_function(FILE *file, shared_memory_t *shared_memory, semaphores_t *semaphores, config_t *config) {
    int id = shared_memory->oxygen_id++;
    print_to_file(file, shared_memory, semaphores, "O %d: started\n", id);
    usleep(random_number(config->wait_time)*1000);      // Simulace čekání atomů
    print_to_file(file, shared_memory, semaphores, "O %d: going to queue\n", id);
    sem_wait(semaphores->mutex);
    shared_memory->oxygens += 1;
    if (shared_memory->hydrogens >= 2) {
        //!Je dostatek atomů vodíků a kyslíků na vytvoření molekuly
        shared_memory->molecule_id++;
        sem_post(semaphores->hydroQueue);
        sem_post(semaphores->hydroQueue);               // Otevření fronty pro 2 vodíky
        shared_memory->hydrogens -= 2;
        sem_post(semaphores->oxyQueue);                 // Otevření fronty pro 1 kyslík
        shared_memory->oxygens -= 1;
    } else {
        sem_post(semaphores->mutex);
    }
    sem_wait(semaphores->oxyQueue);                     // Kyslíky čekají ve frontě na vodíky
    if(shared_memory->shutdown == -1){
        print_to_file(file, shared_memory, semaphores, "O %d: not enough H\n", id);
        return;
    }
    int molecule = shared_memory->molecule_id;
    print_to_file(file, shared_memory, semaphores, "O %d: creating molecule %d\n", id, molecule);
    usleep(random_number(config->bond_time)*1000);      // Simulace tvoření molekuly
    sem_post(semaphores->mol_created);                  // Signalizování vodíkům, že byla vytvořena molekula
    sem_post(semaphores->mol_created);
    print_to_file(file, shared_memory, semaphores, "O %d: molecule %d created\n", id, molecule);
    bond(shared_memory, semaphores);
    sem_wait(semaphores->barrier);
    if(shared_memory->molecule_id == shared_memory->max_molecules){
        sem_post(semaphores->release_function);         // Při vytvoření poslední molekuly se otevře semafor pro uvolňovací funkci
    }
    sem_post(semaphores->mutex);
}
void hydrogen_function(FILE *file, shared_memory_t *shared_memory, semaphores_t *semaphores, config_t *config) {
    int id = shared_memory->hydrogen_id++;
    print_to_file(file, shared_memory, semaphores, "H %d: started\n", id);
    usleep(random_number(config->wait_time)*1000);      // Simulace čekání atomů
    print_to_file(file, shared_memory, semaphores, "H %d: going to queue\n", id);
    sem_wait(semaphores->mutex);
    shared_memory->hydrogens += 1;
    if (shared_memory->hydrogens >= 2 && shared_memory->oxygens >= 1) {
        //!Je dostatek atomů vodíků a kyslíků na vytvoření molekuly
        shared_memory->molecule_id++;
        sem_post(semaphores->hydroQueue);
        sem_post(semaphores->hydroQueue);               // Otevření fronty pro 2 vodíky
        shared_memory->hydrogens -= 2;
        sem_post(semaphores->oxyQueue);                 // Otevření fronty pro 1 kyslík
        shared_memory->oxygens -= 1;
    } else {
        sem_post(semaphores->mutex);
    }
    sem_wait(semaphores->hydroQueue);                   // Vodíky čekají ve frontě na kyslík nebo další vodík
    if(shared_memory->shutdown == -1){
        print_to_file(file, shared_memory, semaphores, "H %d: not enough O or H\n", id);
        return;
    }
    int molecule = shared_memory->molecule_id;
    print_to_file(file, shared_memory, semaphores, "H %d: creating molecule %d\n", id, molecule);
    sem_wait(semaphores->mol_created);                  // Vodíky čekají na kyslík až vytvoří molekulu (skončí usleep)
    print_to_file(file, shared_memory, semaphores, "H %d: molecule %d created\n", id, molecule);
    bond(shared_memory, semaphores);
    sem_wait(semaphores->barrier);
}

void bond(shared_memory_t *shared_memory, semaphores_t *semaphores){
    //!Funkce čeká na průchod 2 vodíků a 1 kyslíku a následně otevře barieru
    sem_wait(semaphores->bond_mutex);
    if(++shared_memory->bond == 3){
        shared_memory->bond = 0;
        for (int i = 0; i < 3; ++i) {                   // Otevře barrieru jakmile jsou na barieře 3 prvky
            sem_post(semaphores->barrier);
        }
    }
    sem_post(semaphores->bond_mutex);
}
void shutdown_processes(shared_memory_t *shared_memory, semaphores_t *semaphores, config_t *config){
    shared_memory->shutdown = -1;
    for (unsigned int i = 0; i < config->oxygen_amount-shared_memory->max_molecules; ++i) {
        sem_post(semaphores->oxyQueue);                 // Uvolní přebytečné kyslíky
    }
    for (unsigned int i = 0; i < config->hydrogen_amount-(shared_memory->max_molecules*2); ++i) {
        sem_post(semaphores->hydroQueue);               // Uvolní přebytečné vodíky
    }
}
unsigned int random_number(unsigned int value) {
    if(value == 0){
        return 0;                                       // Podmínka, aby se nedělilo 0
    }
    return (rand() % value) + 1;
}