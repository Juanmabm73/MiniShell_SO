#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include "parser.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_COMMANDS 20

int child_number; // hacemos como en relevos y la i
char prompt[1024] = "msh> ";

//----------------------FUNCIONES COMPLEMENTARIAS----------------------
// ---------------------------------------------------------------------------------MANEJADOR SEÑAL SIGINT
void sigint_handler()
{
    write(STDOUT_FILENO, "\n", 1); // establecemos una nueva línea
    printf("%s", prompt);          // imprimimos el prompt de la minishell
    fflush(stdout);                // limpia el buffer de salida
}

// ---------------------------------------------------------------------------------MANEJADOR SEÑAL SIGTSTP
void sigtstp_handler()
{ // lo que tenemos que conseguir es que al hacer CTRL + Z la minishell no se pare
    printf("\n");
    printf("Suspender procesos en primer plano no esta implementado\n");
    printf("%s", prompt);
    fflush(stdout);
}

// ---------------------------------------------------------------------------------CREAR ARRAY DE PIDS
int *create_pids_vector(int N)
{
    int *pids_vector = (pid_t *)malloc(N * sizeof(pid_t)); // reservamos memoria para n pids
    printf("Array de pids creado correctamente \n");
    fflush(stdout);
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
        printf("Pipe %d creada correctamente \n", i);
        fflush(stdout);
    }

    printf("Pipes creadas con exito \n");
    fflush(stdout);
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
            printf("Cerrando pipe %d, extremo 0 \n", j);
            fflush(stdout);
            if (close(pipes_vector[j][0]) == -1)
            {
                perror("Error al cerrar descriptor de lectura de pipe");
                exit(1);
            }
        }
        if (j != i)
        { // No cerrar el extremo de escritura de la pipe actual
            printf("Cerrando pipe %d, extremo 1 \n", j);
            fflush(stdout);
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
        printf("Proceso hijo %d: Redirigiendo salida al pipe\n", i);
        fflush(stdout); // primer mandato
        if (dup2(pipes_vector[i][1], STDOUT_FILENO) == -1)
        { // redirigimos su salida al extremo de escritura [1] de la primera pipe
            perror("Error en dup2 (proceso 0)");
            exit(1);
        }
    }
    else if (i == N - 1)
    { // ultimo mandato
        printf("Proceso hijo %d: Redirigiendo entrada del pipe\n", i);
        fflush(stdout);
        if (dup2(pipes_vector[i - 1][0], STDIN_FILENO) == -1)
        {
            perror("Error en dup2 (último mandato)");
            exit(1);
        }
    }
    else
    { // mandato intermedio
        printf("Proceso hijo %d: Redirigiendo entrada y salida del pipe\n", i);
        fflush(stdout);
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
        perror("cd error: ");
    }
    else
    {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
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
            execute_cd_command(token);
        }
    }
}

// ---------------------------------------------------------------------------------EXIT
void exit_shell()
{
    printf("Saliendo de la minishell...\n");
    exit(0);
}

// ---------------------------------------------------------------------------------EJECUTAR COMANDO/S
void execute_commands(char input[1024])
{
    {

        tline *line = tokenize(input);
        int N = line->ncommands;
        int i;

        for (i = 0; i < N; i++)
        {
            printf("Comandos: %s \n", line->commands[i].filename);
            fflush(stdout);
        }

        //-----------------------------------------------------------------------------

        pid_t pid;
        pid_t *pids_vector = create_pids_vector(N); // puntero al vector de pids
        int **pipes_vector = NULL;
        if (N > 1)
        {
            pipes_vector = create_pipes_vector(N);
        }

        // ------------------------------------------------------------------------------

        // CREACION DE PROCESOS HIJOS Y LA DE DIOS
        int i;
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
                printf("Hola soy el proceso hijo %d \n", i);
                fflush(stdout);

                if (N > 1)
                {
                    // cerramos los descriptores de los pipes que no vamos a usar
                    close_descriptors(N, i, pipes_vector);

                    // funcion redirigir
                    redirect_pipes(N, i, pipes_vector);
                }

                fprintf(stderr, "Todo cerrado y redireccionado con exito vamos con el exec de: %s \n", line->commands[i].filename);
                fflush(stdout);
                execvp(line->commands[i].filename, line->commands[i].argv);
                perror("ERROR AL EJECUTAR EL COMANDO");
                exit(1);
            }
            else
            {
                printf("Hola soy el padre\n");
                fflush(stdout);
                pids_vector[i] = pid; // nos guardamos el pid del hijo en su posición
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

        // como buen padre espera a todos sus hijos
        for (i = 0; i < N; i++)
        {
            wait(NULL);
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
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    int i;

    char input[1024];
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
        else
        {
            execute_commands(input);
        }

        printf("%s", prompt);
        fflush;
    }

    return 0;
}