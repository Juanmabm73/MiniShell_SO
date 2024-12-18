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
#include <sys/stat.h>

#define MAX_COMMANDS 20

typedef struct
{
    pid_t pid;          // numero de proceso
    int job_id;         // numero de job (numero de lista)
    char state[20];     // guardamos el estado "running", "stopped", "done"
    char command[1024]; // linea de comando que nos pasan
    pid_t *child_pids;  // array dinamico guarda todos los pids de los hijos
    int childs;         // numero de hijos
    int stopped;
} Job;

Job *jobs = NULL;    // array que nos guardará los jobs, dinámico
int jobs_number = 0; // número de jobs en el array

int child_number; // hacemos como en relevos y la i
char prompt[1024] = "msh> ";

int *pids_vector; // puntero al vector de pids

int N;
char input[1024];

// ---------------------------------------------------------------------------------CREAR ARRAY DE PIDS
int *create_pids_vector(int N)
{
    int *pids_vector = (pid_t *)malloc(N * sizeof(pid_t)); // reservamos memoria para n pids

    if (!pids_vector)
    {
        fprintf(stderr, "Error al reservar memoria para los pids");
    }

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
            return (NULL);
        }
    }

    // creamos las pipes
    for (i = 0; i < N - 1; i++)
    {
        if (pipe(pipes_vector[i]) == -1)
        {
            perror("Error al crear la pipe");
            return NULL;
        }
    }

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
            if (close(pipes_vector[j][0]) == -1)
            {
                perror("Error al cerrar descriptor de lectura de pipe");
                return;
            }
        }
        if (j != i)
        { // No cerrar el extremo de escritura de la pipe actual
            if (close(pipes_vector[j][1]) == -1)
            {
                perror("Error al cerrar descriptor de escritura de pipe");
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------------REDIRIGIR DESCRIPTORES
void redirect_pipes(int N, int i, int **pipes_vector)
{
    if (i == 0)
    {
        if (dup2(pipes_vector[i][1], STDOUT_FILENO) == -1)
        { // redirigimos su salida al extremo de escritura [1] de la primera pipe
            perror("Error en dup2 (proceso 0)");
            fflush(stdout);
            return;
        }
    }
    else if (i == N - 1)
    { // ultimo mandato
        // printf("Proceso hijo %d: Redirigiendo entrada del pipe\n", i);
        // fflush(stdout);
        if (dup2(pipes_vector[i - 1][0], STDIN_FILENO) == -1)
        {
            perror("Error en dup2 (último mandato)");
            return;
        }
    }
    else
    { // mandato intermedio
        // printf("Proceso hijo %d: Redirigiendo entrada y salida del pipe\n", i);
        // fflush(stdout);
        if (dup2(pipes_vector[i - 1][0], STDIN_FILENO) == -1)
        {
            perror("Error en dup2 (mandato intermedio, entrada)");
            return;
        }
        if (dup2(pipes_vector[i][1], STDOUT_FILENO) == -1)
        {
            perror("Error en dup2 (mandato intermedio, salida)");
            return;
        }
    }
}

// ---------------------------------------------------------------------------------EJECUTAR COMANDO CD
void execute_cd_command(char *rute)
{
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
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            printf("El directorio actual es: %s \n", cwd);
            if (setenv("PWD", cwd, 1) == -1)
            {
                perror("Error al actualizar la variable de entorno PWD");
            }; // actualizamos el valor de PWD
            snprintf(prompt, sizeof(prompt), "msh:%.1016s > ", cwd); // limita el tamaño de cwd a 1016
        }
        else
        {
            perror("getcwd error: ");
        }
    }
}

// ----------------------FUNCIONES PRINCIPALES----------------------
// ---------------------------------------------------------------------------------FUNCION CD
void cd_function(tline *line)
{
    if (line->commands->argc == 1) // en caso de que no haya nada
    {
        execute_cd_command(NULL);
    }
    else if (line->commands->argc == 2) // en caso de que haya una ruta
    {
        execute_cd_command(line->commands->argv[1]);
    }
    else
    {
        fprintf(stderr, "Error en el numero de argumentos \n");
        return;
    }
}

// ---------------------------------------------------------------------------------EXIT
void exit_shell()
{
    printf("Saliendo de la minishell...\n");
    exit(0);
}
//----------------------JOBS----------------------
// ---------------------------------------------------------------------------------AÑADIR JOB
void add_job(pid_t *pids_vector, char *command, int num_childs)
{
    int i;

    jobs = realloc(jobs, (jobs_number + 1) * sizeof(Job)); // reservamos memoria para un Job mas, redimensionamos el array
    if (!jobs)
    {
        fprintf(stderr, "Error al reservar memoria para los jobs");
        return;
    }

    // rellenamos el nuevo job
    jobs[jobs_number].pid = pids_vector[0];
    jobs[jobs_number].job_id = jobs_number;
    strcpy(jobs[jobs_number].state, "running");                                     // Estado inicial
    strncpy(jobs[jobs_number].command, command, sizeof(jobs[jobs_number].command)); // Comando ejecutado
    jobs[jobs_number].stopped = 0;

    jobs[jobs_number].child_pids = realloc(jobs[jobs_number].child_pids, (num_childs * sizeof(pid_t)));
    if (!jobs[jobs_number].child_pids)
    {
        fprintf(stderr, "Error al reservar memoria para los pids de los hijos");
        return;
    }

    for (i = 0; i < num_childs; i++)
    {
        jobs[jobs_number].child_pids[i] = pids_vector[i]; // rellenamos los pids de para cada hijo de la linea
    }
    jobs[jobs_number].childs = num_childs;

    jobs_number += 1;
}

// ---------------------------------------------------------------------------------MOSTRAR JOBS
void show_jobs_list()
{
    int i;
    for (i = 0; i < jobs_number; i++)
    {
        printf("[%d] %s ---->        %s \n", jobs[i].job_id, jobs[i].state, jobs[i].command);
    }
}

// ---------------------------------------------------------------------------------REVISAR JOBS
void review_bg()
{
    int i;
    int liberar = 0; // si se queda en 0 han acabado todos
    int n = jobs_number;
    int j;
    pid_t result;

    for (i = 0; i < n; i++)
    { // itera en los jobs
        if (strcmp(jobs[i].state, "Done") != 0)
        {

            for (j = 0; j < jobs[i].childs; j++)
            {                                                           // itera dentro del job la lista de pids
                result = waitpid(jobs[i].child_pids[j], NULL, WNOHANG); // esperamos pero no bloqueamos
                if (result == 0)
                {
                    liberar = 1;
                }
            }
            if (!liberar)
            {
                strcpy(jobs[i].state, "Done");
            }
        }
    }
}

// ---------------------------------------------------------------------------------REANUDAR PROCESO (BG)
void bg(tline *line)
{
    int i = 0;
    int j;
    int n;

    int id;

    if (line->commands->argc == 1)
    {
        for (i = jobs_number - 1; i >= 0; i--)
        {
            if (strcmp(jobs[i].state, "stopped") == 0)
            {
                n = jobs[i].childs;
                for (j = 0; j < n; j++)
                {
                    if (kill(jobs[i].child_pids[j], SIGCONT) == -1)
                    {
                        fprintf(stderr, "Error al reanudar el proceso %d. %s\n", i, strerror(errno));
                        return;
                    }
                }
                strcpy(jobs[i].state, "running");
                return;
            }
        }
    }
    else if (line->commands->argc == 2)
    {
        id = atoi(line->commands->argv[1]);
        if (id >= jobs_number || id < 0)
        {
            fprintf(stderr, "Error en id del job \n");
            return;
        }

        n = jobs[id].childs;
        for (i = 0; i < n; i++)
        {
            if (kill(jobs[id].child_pids[i], SIGCONT) == -1)
            {
                fprintf(stderr, "Error al reanudar el proceso %d. %s\n", id, strerror(errno));
                return;
            }
        }
        strcpy(jobs[id].state, "running");
        show_jobs_list();
    }
    else
    {
        fprintf(stderr, "Error en el numero de argumentos \n");
    }
}

// ----------------------REDIRECCIONES----------------------
// ---------------------------------------------------------------------------------REDIRECCIONAR ENTRADA
void redirect_input_file(char *file)
{
    fprintf(stderr, "File: %s \n", file);

    int f = open(file, O_RDONLY);
    if (f == -1)
    {
        fprintf(stderr, "Error. %s \n", strerror(errno));
        return;
    }
    else
    {
        if (dup2(f, STDIN_FILENO) == -1)
        {
            fprintf(stderr, "Error. %s\n", strerror(errno));
            return;
        }
    }
    close(f);
}

// ---------------------------------------------------------------------------------REDIRECCIONAR SALIDA
void redirect_output_file(char *file)
{
    int f = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f == -1)
    {
        fprintf(stderr, "Error. %s \n", strerror(errno));
        return;
    }
    else
    {
        if (dup2(f, STDOUT_FILENO) == -1)
        {
            fprintf(stderr, "Error. %s\n", strerror(errno));
            return;
        }
    }
    close(f);
}

// ---------------------------------------------------------------------------------REDIRECCIONAR SALIDA DE ERROR
void redirect_output_error_file(char *file)
{
    int f = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f == -1)
    {
        fprintf(stderr, "Error. %s \n", strerror(errno));
        return;
    }
    else
    {
        if (dup2(f, STDERR_FILENO) == -1 && (dup2(f, STDOUT_FILENO)))
        {
            fprintf(stderr, "Error. %s\n", strerror(errno));
            return;
        }
    }
    close(f);
}

// ---------------------------------------------------------------------------------EJECUTAR COMANDO/S
void execute_commands(tline *line)
{
    pid_t pid;
    int **pipes_vector = NULL;

    N = line->ncommands;
    int i;
    int num_childs = line->ncommands; // numero de hijos
    int valid_line = 0;

    // hacemos la comprobación de que todos los comandos en la línea son validos en el momento que cambia a 1 valid_line salimos y volveriamos a mostrar el prompt
    i = 0;
    while (i < N && valid_line == 0)
    {
        if (line->commands[i].filename == NULL)
        {
            valid_line = 1;
        }
        i++;
    }

    if (valid_line == 1)
    {
        fprintf(stderr, "Error, la linea de comandos es incorrecta \n");
        return;
    }

    //-----------------------------------------------------------------------------

    pids_vector = create_pids_vector(N); // puntero al vector de pids
    if (N > 1)
    {
        pipes_vector = create_pipes_vector(N);
    }

    // ------------------------------------------------------------------------------

    // CREACION DE PROCESOS HIJOS
    for (i = 0; i < N; i++)
    {
        child_number = i; // guardamos el id del hijo para tenerlo identificado despues de este for
        pid = fork();

        // comprobamos si se ha creado bien el proceso hijo
        if (pid < 0)
        {
            fprintf(stderr, "Error al crear proceso hijo \n");
            return;
        }
        else if (pid == 0) // si es el hijo
        {
            if (line->background == 1)
            {
                signal(SIGINT, SIG_IGN); // cuando el hijo recibe estas señales las trata diferentes al padre
                signal(SIGTSTP, SIG_IGN);
            }
            else
            {
                signal(SIGINT, SIG_DFL); // cuando el hijo recibe estas señales las trata diferentes al padre
                signal(SIGTSTP, SIG_DFL);
            }

            if ((i == 0) && (line->redirect_input != NULL))
            {
                redirect_input_file(line->redirect_input);
            }
            if (i == N - 1 && (line->redirect_error != NULL))
            {
                redirect_output_error_file(line->redirect_error);
            }
            if (i == N - 1 && (line->redirect_output != NULL))
            {
                redirect_output_file(line->redirect_output);
            }

            if (N > 1)
            {
                // cerramos los descriptores de los pipes que no vamos a usar
                close_descriptors(N, i, pipes_vector);

                // funcion redirigir
                redirect_pipes(N, i, pipes_vector);
            }

            execvp(line->commands[i].filename, line->commands[i].argv);
            // si imprime esto significa que el exec no se hizo bien
            fprintf(stderr, "No se ha encontrado el comando %s", line->commands[i].filename);
            fflush(stderr);
            return;
        }
        else // Padre
        {

            pids_vector[i] = pid; // nos guardamos el pid del hijo en su posición en array global de pids
        }
    }

    // una vez fuera del for cerramos todos los descriptores de los pipes ya que el padre no usa ninguno y si lo cerramos antes los hijos heredan descriptores cerrados cosa que daría fallos
    if (N > 1)
    {
        for (i = 0; i < N - 1; i++)
        {
            close(pipes_vector[i][0]);
            close(pipes_vector[i][1]);
        }
    }

    if (line->background == 0) // si el comando se ejecuta en primer plano
    {
        // fprintf(stderr, "Foreground, Esperando a los hijos\n");
        // fflush(stdout);
        for (i = 0; i < N; i++)
        {
            if (waitpid(pids_vector[i], NULL, WUNTRACED) == -1)
            {
                perror("Error en waitpid");
            }; // Como buen padre esperamos que todos los hijos terminen
        }
    }
    else // comando en background
    {
        // no bloqueamos la ejecución y al salir comprobamos el estado de los hijos con waitpid y WNHANG
        add_job(pids_vector, input, num_childs);
        fprintf(stderr, "[%d] %d\n", jobs[jobs_number - 1].job_id, jobs[jobs_number - 1].pid);
    }

    // Liberar memoria al final
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

// ---------------------------------------------------------------------------------UMASK
void umask_function(tline *line)
{
    mode_t new_mask; // almacena la nueva máscara de permisos

    // Verificamos el número de tokens obtenidos
    if (line->commands->argc == 1)
    {
        // imprimimos la máscara actual
        mode_t current_mask = umask(0); // establecemos máscara actual a 0
        umask(current_mask);            // Restauramos la máscara anterior
        printf("%04o\n", current_mask);
        fflush(stdout);
    }
    else if (line->commands->argc == 2)
    {
        // Verificar si el token es una máscara válida
        char *endptr;
        new_mask = strtol(line->commands->argv[1], &endptr, 8); // Convertir el token a un número en base 8

        if (*endptr != '\0' || new_mask > 0777)
        {
            fprintf(stderr, "Error: La máscara proporcionada no es válida.\n");
            return;
        }

        // Aplicar la nueva máscara
        umask(new_mask);
    }
    else
    {
        fprintf(stderr, "Error en el numero de comandos\n");
        return;
    }
}

// ----------------------MANEJADORES DE SEÑALES----------------------
// ---------------------------------------------------------------------------------MANEJADORES SEÑALES FOREGROUND
// ---------------------------------------------------------------------------------CTRL C
void sigint_handler()
{
    int i;

    if (pids_vector != NULL)
    {
        for (i = 0; i < N; i++)
        {
            if (kill(pids_vector[i], SIGINT) == -1)
            {
                fprintf(stderr, "Error al enviar señal CTRL C al hijo \n");
            }
        }
    }
    else
    {
        fprintf(stdout, "No hay ningún proceso que parar \n");
    }
    printf("\n");
}

// ---------------------------------------------------------------------------------MANEJADOR SEÑAL SIGTSTP
// ---------------------------------------------------------------------------------CTRL Z

void sigtstp_handler()
{
    int i;

    if (pids_vector != NULL)
    {
        for (i = 0; i < N; i++)
        {
            if (kill(pids_vector[i], SIGTSTP) != -1)
            {
                add_job(pids_vector, input, N);
                strcpy(jobs[jobs_number - 1].state, "stopped");
                jobs[jobs_number - 1].stopped = 1;
                fprintf(stderr, "[%d] %d\n", jobs[jobs_number - 1].job_id, jobs[jobs_number - 1].pid);
                show_jobs_list();
            }
        }
    }
    else
    {
        fprintf(stdout, "No hay ningún proceso que parar \n");
    }
}

// -------------------REDIRECCIONES-------------------
// ---------------------------------------------------------------------------------REDIRECCIONES A FIC
void redirections_to_file(tline *line)
{
    if ((line->redirect_input != NULL))
    {
        redirect_input_file(line->redirect_input);
    }
    if ((line->redirect_error != NULL))
    {
        redirect_output_error_file(line->redirect_error);
    }
    if ((line->redirect_output != NULL))
    {
        redirect_output_file(line->redirect_output);
    }
}

void redirections_to_standar()
{
    int fd;

    fd = open("/dev/tty", O_RDONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error al restaurar la entrada estandar. %s\n", strerror(errno));
        return;
    }
    if (dup2(fd, STDIN_FILENO) == -1)
    {
        fprintf(stderr, "Error al restaurar la entrada estándar. %s\n", strerror(errno));
        close(fd);
        return;
    }
    close(fd);

    fd = open("/dev/tty", O_WRONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error al restaurar la salida estándar. %s\n", strerror(errno));
        return;
    }
    if (dup2(fd, STDOUT_FILENO) == -1)
    {
        fprintf(stderr, "Error al restaurar la salida estándar. %s\n", strerror(errno));
        close(fd);
        return;
    }
    close(fd);

    // Redirigir la salida de error estándar (stderr) de nuevo a la consola
    fd = open("/dev/tty", O_WRONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error al restaurar la salida de error. %s\n", strerror(errno));
        return;
    }
    if (dup2(fd, STDERR_FILENO) == -1)
    {
        fprintf(stderr, "Error al restaurar la salida de error. %s\n", strerror(errno));
        close(fd);
        return;
    }
    close(fd);
}

// ----------------------FUNCION MAIN----------------------
// ---------------------------------------------------------------------------------FUNCIÓN MAIN
int main()
{
    char input_cpy[1024];
    tline *line;
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    printf("%s", prompt);
    fflush(stdout);
    while (fgets(input, sizeof(input), stdin) != NULL)
    {
        strcpy(input_cpy, input);
        line = tokenize(input);
        if (line == NULL)
        {
            fprintf(stderr, "Error: Fallo al tokenizar la línea de comandos.\n");
            return 1;
        }

        N = 0;

        redirections_to_file(line);

        if (strcmp(input_cpy, "\n") == 0)
        {
            printf("Entrada vacía. Saliendo...\n");
        }
        else if (strncmp(input_cpy, "cd", 2) == 0)
        {
            cd_function(line);
        }
        else if (strncmp(input_cpy, "exit", 4) == 0)
        {
            exit_shell();
        }
        else if (strncmp(input_cpy, "umask", 5) == 0)
        {
            umask_function(line);
        }
        else if (strncmp(input_cpy, "jobs", 4) == 0)
        {
            review_bg(); // para que me de tiempo a cambiarlo
            show_jobs_list();
        }
        else if (strncmp(input_cpy, "bg", 2) == 0)
        {
            bg(line);
        }
        else
        {
            execute_commands(line);
        }

        review_bg();

        redirections_to_standar();

        printf("%s", prompt);
        fflush(stdout);
    }

    return 0;
}