#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include "parser.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_COMMANDS 20

typedef struct
{
    pid_t pid;          // numero de proceso
    int job_id;         // numero de job (numero de lista)
    char state[20];     // guardamos el estado "running", "stopped", "done"
    char command[1024]; // linea de comando que nos pasan
    pid_t *child_pids;  // array dinamico guarda todos los pids de los hijos
    int childs;         // numero de hijos
} Job;

Job *jobs = NULL;    // array que nos guardará los jobs, dinámico
int jobs_number = 0; // número de jobs en el array

int child_number; // hacemos como en relevos y la i
char prompt[1024] = "msh> ";

pid_t pid;          // pid del proceso
int *pids_vector; // puntero al vector de pids
int **pipes_vector = NULL;

int N;
char input[1024];

// ----------------------MANEJADORES DE SEÑALES----------------------
// ---------------------------------------------------------------------------------MANEJADORES SEÑALES FOREGROUND
// ---------------------------------------------------------------------------------CTRL C
void sigint_handler()
{
    int i;
    // pid_t fg_pgid = tcgetpgrp(STDIN_FILENO);
    fprintf(stderr, "Estoy en ctrl c el valor de N es: %d", N);
    
    if (pids_vector != NULL){

        for (i = 0; i < N; i++)
        {
            if (kill(pids_vector[i], SIGINT) == -1){
                fprintf(stderr, "Error al enviar señal CTRL C al hijo \n");
            } else{
                fprintf(stderr, "Señal CTRL C enviada correctamente \n");
            }   
                
        }
    } else {
        fprintf(stderr, "El array de pids esta vacio \n");
    }

    // fprintf(stdout, "\n%s", prompt);
    // fflush(stdout); // Asegúrate de que el prompt se imprima inmediatamente
}

// ---------------------------------------------------------------------------------MANEJADOR SEÑAL SIGTSTP
// ---------------------------------------------------------------------------------CTRL Z

void sigtstp_handler()
{
    int i;
    pid_t fg_pgid = tcgetpgrp(STDIN_FILENO);

    for (i = 0; i < jobs_number; i++)
    {
        kill(jobs[i].pid, SIGTSTP);
        strcpy(jobs[i].state, "stopped");
    }
    fprintf(stdout, "\n%s", prompt);
    fflush(stdout); // Asegúrate de que el prompt se imprima inmediatamente
}

// ---------------------------------------------------------------------------------CREAR ARRAY DE PIDS
int *create_pids_vector(int N)
{
    int *pids_vector = (pid_t *)malloc(N * sizeof(pid_t)); // reservamos memoria para n pids
    // printf("Array de pids creado correctamente \n");
    // fflush(stdout);
    return pids_vector;
}

// ---------------------------------------------------------------------------------CREAR ARRAY DE PIPES
int **create_pipes_vector(int N)
{
    int i;
    int **pipes_vector;
    pipes_vector = (int **)malloc((N - 1) * sizeof(int *)); // reservamos memoria para N-1 mandatos

    // dentro de cada hueco para la pipe reservamos memoria para sus dos descriptores
    for (i = 0; i < N - 1; i++)
    {
        pipes_vector[i] = (int *)malloc(2 * sizeof(int));
        if (!pipes_vector[i])
        {
            fprintf(stderr, "Error al reservar memoria en las pipes");
            exit(1);
        }
    }

    // creamos las pipes
    for (i = 0; i < N - 1; i++)
    {
        if (pipe(pipes_vector[i]) == -1)
        {
            perror("Error al crear la pipe");
            exit(1);
        }
        // printf("Pipe %d creada correctamente \n", i);
        // fflush(stdout);
    }

    // printf("Pipes creadas con exito \n");
    // fflush(stdout);
    return pipes_vector;
}

// ---------------------------------------------------------------------------------CERRAR DESCRIPTORES
void close_descriptors(int N, int i, int **pipes_vector)
{
    int j;
    for (j = 0; j < N - 1; j++)
    {
        if (j != i - 1)
        { // No cerrar el extremo de lectura de la pipe anterior
            // printf("Cerrando pipe %d, extremo 0 \n", j);
            // fflush(stdout);
            if (close(pipes_vector[j][0]) == -1)
            {
                perror("Error al cerrar descriptor de lectura de pipe");
                exit(1);
            }
        }
        if (j != i)
        { // No cerrar el extremo de escritura de la pipe actual
            // printf("Cerrando pipe %d, extremo 1 \n", j);
            // fflush(stdout);
            if (close(pipes_vector[j][1]) == -1)
            {
                perror("Error al cerrar descriptor de escritura de pipe");
                exit(1);
            }
        }
    }
}

// ---------------------------------------------------------------------------------REDIRIGIR DESCRIPTORES
void redirect_pipes(int N, int i, int **pipes_vector)
{
    if (i == 0)
    {
        // printf("Proceso hijo %d: Redirigiendo salida al pipe\n", i);
        // fflush(stdout); // primer mandato
        if (dup2(pipes_vector[i][1], STDOUT_FILENO) == -1)
        { // redirigimos su salida al extremo de escritura [1] de la primera pipe
            perror("Error en dup2 (proceso 0)");
            fflush(stdout);
            exit(1);
        }
    }
    else if (i == N - 1)
    { // ultimo mandato
        // printf("Proceso hijo %d: Redirigiendo entrada del pipe\n", i);
        // fflush(stdout);
        if (dup2(pipes_vector[i - 1][0], STDIN_FILENO) == -1)
        {
            perror("Error en dup2 (último mandato)");
            exit(1);
        }
    }
    else
    { // mandato intermedio
        // printf("Proceso hijo %d: Redirigiendo entrada y salida del pipe\n", i);
        // fflush(stdout);
        if (dup2(pipes_vector[i - 1][0], STDIN_FILENO) == -1)
        {
            perror("Error en dup2 (mandato intermedio, entrada)");
            exit(1);
        }
        if (dup2(pipes_vector[i][1], STDOUT_FILENO) == -1)
        {
            perror("Error en dup2 (mandato intermedio, salida)");
            exit(1);
        }
    }
}

// ---------------------------------------------------------------------------------EJECUTAR COMANDO CD
void execute_cd_command(char *rute)
{

    printf("La ruta que llega a cd es: %s \n", rute);
    fflush(stdout);

    if (rute == NULL)
    {
        rute = getenv("HOME");
        if (rute == NULL)
        {
            fprintf(stderr, "No se ha podido encontrar el directorio HOME");
        }
    }

    if (chdir(rute) == -1)
    {
        fprintf(stdout, "No entra en el direcotorio:%s, %s \n", rute, strerror(errno));
    }
    else
    {
        printf("Se ha podido redirigir al directorio \n");
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            printf("El directorio actual es: %s \n", cwd);
            setenv("PWD", cwd, 1); // actualizamos el valor de PWD
            snprintf(prompt, sizeof(prompt), "msh:%s >", cwd);
        }
        else
        {
            perror("getcwd error: ");
        }
    }
}

// ----------------------FUNCIONES PRINCIPALES----------------------
// ---------------------------------------------------------------------------------FUNCION CD
void cd_function(char input[1024])
{
    char *token;
    // Tokenizar la cadena por espacios
    token = strtok(input, " "); // El primer token será "cd"
    token = strtok(NULL, " ");  // El siguiente token debería ser el directorio

    // Verificamos el número de tokens obtenidos
    if (token != NULL)
    {
        // Si hay un segundo token, pasamos a la función execute_cd_command
        // Verificar si hay más de dos partes
        if (strtok(NULL, " ") != NULL)
        {
            printf("Error: El comando 'cd' solo acepta un argumento.\n");
        }
        else
        {
            // Llamamos a la función para cambiar al directorio
            token[strcspn(token, "\n")] = '\0';
            execute_cd_command(token);
        }
    }
    else
    {
        execute_cd_command(NULL);
    }
}

// ---------------------------------------------------------------------------------EXIT
void exit_shell()
{
    printf("Saliendo de la minishell...\n");
    exit(0);
}

// ---------------------------------------------------------------------------------JOBS
void add_job(pid_t *pids_vector, char *command, int num_childs)
{
    int i;
    jobs = realloc(jobs, (jobs_number + 1) * sizeof(Job)); // reservamos memoria para un Job mas, redimensionamos el array

    // rellenamos el nuevo job
    jobs[jobs_number].pid = pids_vector[0];
    jobs[jobs_number].job_id = jobs_number;
    strcpy(jobs[jobs_number].state, "running");                                     // Estado inicial
    strncpy(jobs[jobs_number].command, command, sizeof(jobs[jobs_number].command)); // Comando ejecutado
    jobs[jobs_number].child_pids = realloc(jobs[jobs_number].child_pids, (num_childs * sizeof(pid_t)));
    for (i = 0; i < num_childs; i++)
    {
        jobs[jobs_number].child_pids[i] = pids_vector[i]; // rellenamos los pids de para cada hijo de la linea
    }
    jobs[jobs_number].childs = num_childs;

    jobs_number += 1;
}

void show_jobs_list()
{
    int i;
    for (i = 0; i < jobs_number; i++)
    {
        printf("[%d] %s ---->        %s \n", jobs[i].job_id, jobs[i].state, jobs[i].command);
    }
}

void revisar_bg()
{
    int i;
    int liberar = 0; // si se queda en 0 han acabado todos
    int n = jobs_number;
    int j;
    char status[1024];
    pid_t result;

    for (i = 0; i < n; i++)
    { // itera en los jobs
        if (strcmp(jobs[i].state, "Done") != 0)
        {

            for (j = 0; j < jobs[i].childs; j++)
            {                                                              // itera dentro del job la lista de pids
                result = waitpid(jobs[i].child_pids[j], &status, WNOHANG); // esperamos pero no bloqueamos
                // fprintf(stderr, "Para el proceso hijo %d con pid %d el resultados del waitpid es: %d \n", j, jobs[i].child_pids[j],result);
                if (result == 0)
                {
                    liberar = 1;
                }
            }
            if (!liberar)
            {
                // fprintf(stderr, "ACABADO BACK \n");
                strcpy(jobs[i].state, "Done");
                // printf("[%d] %s \n", jobs[i].job_id, jobs[i].state);
                fflush;
                // tendriamos que ir a liberar la memoria del proceso
            }
        }
    }
}

// ---------------------------------------------------------------------------------EJECUTAR COMANDO/S
void execute_commands(char input[1024], pid_t pid, int **pipes_vector)
{

    int liberar = 0;
    char status[1024];
    tline *line = tokenize(input);
    N = line->ncommands;
    int i;
    int num_childs = line->commands->argc; // numero de hijos

    // for (i = 0; i < N; i++)
    // {
    //     printf("Comandos: %s \n", line->commands[i].filename);
    //     fflush(stdout);
    // }

    //-----------------------------------------------------------------------------

    pids_vector = create_pids_vector(N); // puntero al vector de pids
    if (N > 1)
    {
        pipes_vector = create_pipes_vector(N);
    }

    // ------------------------------------------------------------------------------

    // CREACION DE PROCESOS HIJOS Y LA DE DIOS
    for (i = 0; i < N; i++)
    {
        child_number = i; // guardamos el id del hijo para tenerlo identificado despues de este for
        pid = fork();

        // comprobamos si se ha creado bien el proceso hijo
        if (pid < 0)
        {
            fprintf(stderr, "Error al crear proceso hijo \n");
            exit(1);
        }
        else if (pid == 0) // si es el hijo
        {
            // printf("Hola soy el proceso hijo %d \n", i);
            // fflush(stdout);

            if (N > 1)
            {
                // cerramos los descriptores de los pipes que no vamos a usar
                close_descriptors(N, i, pipes_vector);

                // funcion redirigir
                redirect_pipes(N, i, pipes_vector);
            }

            // fprintf(stderr, "Todo cerrado y redireccionado con exito vamos con el exec de: %s \n", line->commands[i].filename);
            // fflush(stderr);
            if (line->commands[i].filename == NULL)
            {
                fprintf(stderr, "Error: command not found\n");
            }
            else
            {
                execvp(line->commands[i].filename, line->commands[i].argv);
            }
            // fprintf(stderr,"No se ha encontrado el comando %s", line->commands[i].filename);
            // fflush(stderr);
            exit(1);
        }
        else
        {
            // printf("Hola soy el padre\n");
            // fflush(stdout);
            pids_vector[i] = pid; // nos guardamos el pid del hijo en su posición
            fprintf(stderr, "Pid en posicion %d de pids vector es: %d \n",i, pids_vector[i]);
        }
    }

    // cerramos todos los descriptores de los pipes ya que el padre no usa ninguno y si lo cerramos antes los hijos heredan descriptores cerrados cosa que daría fallos
    if (N > 1)
    {
        for (i = 0; i < N - 1; i++)
        {
            close(pipes_vector[i][0]);
            close(pipes_vector[i][1]);
        }
    }

    // si el comando se ejecuta en primer plano
    if (line->background == 0)
    {
        // fprintf(stderr, "Foreground, Esperando a los hijos\n");
        // fflush(stdout);
        for (i = 0; i < N; i++)
        {
            waitpid(pids_vector[i], NULL, 0);
        }
    }
    else
    {
        add_job(pids_vector, input, num_childs);
        fprintf(stderr, "[%d] %d\n", jobs[jobs_number - 1].job_id, jobs[jobs_number - 1].pid);
    }

    // Liberar memoria al final
    if (line->background == 0)
    {

        if (N > 1)
        {
            for (i = 0; i < N - 1; i++)
            {
                free(pipes_vector[i]);
            }
            free(pipes_vector);
        }
        free(pids_vector);
    }
    else
    { // DUDA CUANDO LIBERAMOS ESTA MEMORIA

        if (N > 1)
        {
            for (i = 0; i < N - 1; i++)
            {
                free(pipes_vector[i]);
            }
            free(pipes_vector);
        }
        free(pids_vector);
    }
}

// ---------------------------------------------------------------------------------UMASK
void umask_function(char input[1024])
{
    char *token;
    // Tokenizar la cadena por espacios
    token = strtok(input, " "); // El primer token será el comando "umask"
    token = strtok(NULL, " ");  // El siguiente token debería ser el modo

    // Verificamos el número de tokens obtenidos
    if (token != NULL)
    {
        // verificamos si hay mas de una opcion
        if (strtok(NULL, " ") != NULL)
        {
            printf("Error: El comando 'umask' solo acepta un argumento.\n");
        }
        else
        {
            mode_t new_mask; // almacena la nueva máscara de permisos

            if (token == NULL) // no se proporciona nueva máscara
            {
                // imprimimos la máscara actual
                mode_t current_mask = umask(0); // establecemos máscara actual a 0
                umask(current_mask);            // Restauramos la máscara anterior
                printf("%04o\n", current_mask);
                fflush(stdout);
                printf("%04o\n", current_mask);
                fflush(stdout);
            }
            else
            {
                // Convertir el modo de cadena a número
                new_mask = strtol(token, NULL, 8);    // convertimos la nueva máscara a octal
                if (new_mask == 0 && errno == EINVAL) // si hay error en la conversión
                {
                    perror("Error al convertir el modo");
                }
                else
                {
                    umask(new_mask);                                       // nueva máscara de permisos
                    printf("Nueva máscara establecida: %04o\n", new_mask); // Imprimimos la nueva máscara en formato octal
                }
            }
        }
    }
}

//----------------------FUNCION MAIN----------------------
// ---------------------------------------------------------------------------------FUNCIÓN MAIN
int main()
{
    // int i;

        // printf("%d \n", jobs_number);
    // fflush(stdout);

    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    printf("%s", prompt);
    fflush(stdout);
    while (fgets(input, sizeof(input), stdin) != NULL)
    {
        if (strcmp(input, "\n") == 0)
        {
            printf("Entrada vacía. Saliendo...\n");
        }
        else if (strncmp(input, "cd", 2) == 0)
        {
            printf("Vamos a ejecutar cd con la ruta: %s", input);
            cd_function(input);
        }
        else if (strncmp(input, "exit", 4) == 0)
        {
            exit_shell();
        }
        else if (strncmp(input, "umask", 5) == 0)
        {
            umask_function(input);
        }
        else if (strncmp(input, "jobs", 4) == 0)
        {
            revisar_bg(); // para que me de tiempo a cambiarlo
            show_jobs_list();
        }
        else
        {
            execute_commands(input, pid, pipes_vector);
        }

        revisar_bg();

        printf("%s", prompt);
        fflush;
    }

    return 0;
}