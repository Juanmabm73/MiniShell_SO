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

// ---------------------------------------------------------------------------------MANEJADOR SEÑAL SIGINT
void sigint_handler(int signal)
{
    write(STDOUT_FILENO, "\n", 1); // establecemos una nueva línea
    printf("msh > ");              // imprimimos el prompt de la minishell
    fflush(stdout);                // limpia el buffer de salida
}

// ---------------------------------------------------------------------------------MANEJADOR SEÑAL SIGTSTP
void sigtstp_handler(int signal)
{ // lo que tenemos que conseguir es que al hacer CTRL + Z la minishell no se pare
    printf("\n");
    printf("Suspender procesos en primer plano no esta implementado\n");
    printf("msh > ");
    fflush(stdout);
}

// ---------------------------------------------------------------------------------CREAR ARRAY DE PIDS
int *create_pids_vector(int N)
{
    int *pids_vector = (pid_t *)malloc(N * sizeof(pid_t)); // reservamos memoria para n pids
    printf("Array de pids creado correctamente \n");
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
        if (!pipes_vector[i]) {
            fprintf(stderr, "Error al reservar memoria en las pipes");
        }
    }

    // creamos las pipes
    for (i = 0; i < N - 1; i++)
    {
        
        if (pipe(pipes_vector[i]) == -1){
            printf("Error al crear la pipe %d \n", i);
        }
        printf("Pipe %d creada correctamente \n", i);
        
    }
    
    printf("Pipes creadas con exito \n");
    return pipes_vector;
}

// ---------------------------------------------------------------------------------FUNCIÓN MAIN
int main()
{
    
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    int i;
    int j;

    char input[1024];
    printf("msh> ");
    fgets(input, sizeof(input), stdin);

    // Verificar si la entrada está vacía
    if (input[0] == '\n')
    {
        printf("Entrada vacía. Saliendo...\n");
        return 0;
    }

    tline *line = tokenize(input);
    int N = line->ncommands;
    
    for ( i = 0; i < N; i++)
    {
        printf("Comandos: %s \n", line->commands[i].filename);
    }
    
    

    //-----------------------------------------------------------------------------

    pid_t pid;
    int *pids_vector = create_pids_vector(N); // puntero al vector de pids
    int **pipes_vector;
    if (N > 1) {
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
        }
        else if (pid == 0) // si es el hijo
        {
            printf("Hola soy el proceso hijo %d \n", i);

            
            if (N > 1){


                // cerramos los descriptores de los pipes que no vamos a usar
                for (j = 0; j < N - 1; j++) {
                    if (j != i - 1) { // No cerrar el extremo de lectura de la pipe anterior
                        printf("Cerrando pipe %d, extremo 0 \n", j);
                        if (close(pipes_vector[j][0]) == -1) {
                            perror("Error al cerrar descriptor de lectura de pipe");
                            exit(1);
                        }
                    }
                    if (j != i) { // No cerrar el extremo de escritura de la pipe actual
                        printf("Cerrando pipe %d, extremo 1 \n", j);
                        if (close(pipes_vector[j][1]) == -1) {
                            perror("Error al cerrar descriptor de escritura de pipe");
                            exit(1);
                        }
                    }
                }


                // funcion redirigir
                if (i == 0){         
                    fflush(stdout);                                   // primer mandato
                    printf("Proceso hijo %d: Redirigiendo salida al pipe\n", i);
                    if (dup2(pipes_vector[0][1], STDOUT_FILENO) == -1) { // redirigimos su salida al extremo de escritura [1] de la primera pipe
                        perror("Error en dup2 (proceso 0) \n");
                        exit(1);
                    } else {
                        printf("TODO OK");
                    }
                }
                else if (i == N - 1 ){ // ultimo mandato
                    fflush(stdout);
                    printf("Proceso hijo %d: Redirigiendo entrada del pipe\n", i);
                    if (dup2(pipes_vector[i - 1][0], STDIN_FILENO) == -1) {
                        perror("Error en dup2 (último mandato) \n");
                        exit(1);
                    }
                }
                else{ // mandato intermedio
                    /*

                [0]         [1]         [2]         [3]         [4]         [5]         [6]         [7]
                    =======     =======     =======     =======     =======     =======     =======
                        0           1           2           3           4           5           6

                */
                    fflush(stdout);
                    dup2(pipes_vector[i - 1][0], STDIN_FILENO);
                    dup2(pipes_vector[i][1], STDOUT_FILENO);
                }
            } 

            printf("La i es:%d \n", i);
            printf("Todo cerrado y redireccionado con exito vamos con el exec de: %s \n", line->commands[i].filename);
            execvp(line->commands[i].filename, line->commands[i].argv);
            printf("ERROR AL EJECUTAR EL COMANDO %d \n", i);
        }
        else
        {
            printf("Hola soy el padre \n");
            pids_vector[i] = pid; // nos guardamos el pid del hijo en su posición
        }
    }

    // cerramos todos los descriptores de los pipes ya que el padre no usa ninguno y si lo cerramos antes los hijos heredan descriptores cerrados cosa que daría fallos
    for (i = 0; i < N-1; i++)
    {
        close(pipes_vector[i][0]);
        close(pipes_vector[i][1]);
    }

    // como buen padre espera a todos sus hijos
    for (i = 0; i < N; i++)
    {
        wait(NULL);
    }

    // pendiente hacer free de las pipes y sus descriptores

    // Liberar memoria de pipes
    for (i = 0; i < N - 1; i++)
    {
        free(pipes_vector[i]);
    }
    free(pipes_vector);

   
    free(pids_vector);
    return 0;
}
