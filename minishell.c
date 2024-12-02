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

// ---------------------------------------------------------------------------------DEFINIMOS ESTRUCTURA TLINE
// typedef struct
// {
//     char **tokens; // array de punteros a tokens
//     int ncommands; // numero de comandos en la linea
// } tline;

int child_number; // hacemos como en relevos y la i

// ---------------------------------------------------------------------------------MANEJADOR SEÑAL SIGINT
void sigint_handler(int signal){
    write(STDOUT_FILENO, "\n", 1); // establecemos una nueva línea
    printf("msh > ");              // imprimimos el prompt de la minishell
    fflush(stdout);                // limpia el buffer de salida
}

// ---------------------------------------------------------------------------------MANEJADOR SEÑAL SIGTSTP
void sigtstp_handler(int signal) { // lo que tenemos que conseguir es que al hacer CTRL + Z la minishell no se pare
    printf("\n");
    printf("Suspender procesos en primer plano no esta implementado\n");
    printf("msh > ");
    fflush(stdout);
}

// ---------------------------------------------------------------------------------TOKENIZAR ENTRADA
// tline *tokenize_input(const char *input)
// {
//     tline *line = malloc(sizeof(tline));                  // reservamos memoria dinamica
//     line->tokens = malloc(sizeof(char *) * MAX_COMMANDS); // reservamos memoria dinamixa para un array de punteros a cadenas
//     line->ncommands = 0;
//     int N = line->ncommands;

//     char *input_copy = strdup(input);      // creamos una copia de la entrada para no modificarla
//     char *token = strtok(input_copy, "|"); // creamos un array de tokens donde cada posicion esta separada por un pipe

//     while (token != NULL && N < MAX_COMMANDS)
//     {
//         line->tokens[N + 1] = strdup(token); // copia el token actual en el que nos encontramos en el array de tokens de line
//         token = strtok(NULL, "|");           // le pasamos NULL para que siga por la posición en la que iba
//     }

//     free(input_copy);
//     return line;
// }

// ---------------------------------------------------------------------------------CREAR ARRAY DE PIDS
pid_t *create_pids_vector(int N) {
    int **pids_vector = (pid_t *)malloc(N * sizeof(pid_t)); // reservamos memoria para n pids
    printf("Array de pids creado correctamente \n");
    return pids_vector;
}

// ---------------------------------------------------------------------------------CREAR ARRAY DE PIPES
int **create_pipes_vector(int N) {
    int i;
    int **pipes_vector;
    pipes_vector = (int **)malloc((N - 1) * sizeof(int)); // reservamos memoria para N-1 mandatos

    // dentro de cada hueco para la pipe reservamos memoria para sus dos descriptores
    for (i = 0; i < N - 1; i++){
        pipes_vector[i] = (int *)malloc(2 * sizeof(int));
    }

    // creamos las pipes
    for (i = 0; i < N-1; i++){
        pipe(pipes_vector[i]);
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

    // Eliminar espacios y saltos de línea al final de la entrada
    // size_t len = strlen(input);
    // while (len > 0 && (input[len - 1] == ' ' || input[len - 1] == '\n'))
    // {
    //     input[len - 1] = '\0'; // Elimino el caracter
    //     len--;
    // }
    // esto creo q no es necesario lo de la funcion porq lo hace directamente la libreria creo 
    //tline *line = tokenize_input(input);
    tline *line = tokenize(input); 
    int N = line->ncommands;
    printf("Numero de comandos: %d \n", N);

    //-----------------------------------------------------------------------------

    pid_t pid;
    int **pids_vector = create_pids_vector(N); // puntero al vector de pids
    int **pipes_vector = create_pipes_vector(N);

    // ------------------------------------------------------------------------------

    // CREACION DE PROCESOS HIJOS Y LA DE DIOS

    for (i = 0; i < N; i++){
        child_number = i;                                   // guardamos el id del hijo para tenerlo identificado despues de este for
        pid = fork();

        for (j = 0; j < N-1; j++){
            if (j != i-1){
                close(pipes_vector[i][0]);                  // cerrar extremos de lectura
            } else if (j != i){   
                close(pipes_vector[j][1]);                  // cerrar extremos de escritura
            }
        }
        printf("Todo cerrado correctamente");

        if (pid < 0){
            fprintf(stderr, "Error al crear proceso hijo \n");
        } else if (pid == 0) {
            printf("Hola soy el proceso hijo %d \n", i);
            
            if (i == 0){                                    // primer mandato
                printf("ESTOY DENTRO \n");
                if (dup2(pipes_vector[0][1], STDOUT_FILENO) == -1){    // redirigimos su salida al extremo de escritura [1] de la primera pipe
                    printf("Se ha producido un error %s", errno);
                } else {
                    printf("Redirección completada \n");
                }
                
            } else if ( i == N-1){                          // ultimo mandato
                
                dup2(pipes_vector[i-1][0], STDIN_FILENO);
                printf("Redirección completada \n");
                
            } else {                                        // mandato intermedio
            /* 

            [0]         [1]         [2]         [3]         [4]         [5]         [6]         [7]
                =======     =======     =======     =======     =======     =======     =======    
                    0           1           2           3           4           5           6

            */
                dup2(pipes_vector[i-1][0], STDIN_FILENO);
                dup2(pipes_vector[i][1], STDOUT_FILENO);
                printf("Redirección completada \n");

            }
            printf("Todo cerrado y redireccionado con exito vamos con el exec de: %s \n", line->commands[i].filename);
            execvp(line->commands[i].filename, line->commands[i].argv);
            printf("ERROR AL EJECUTAR EL COMANDO %d \n", i);

        } else {
            printf("Hola soy el padre");
            *pids_vector[i] = pid;                  // nos guardamos el pid del hijo en su posición
            
        }
        
    }
    
    // cerramos todos los descriptores de los pipes ya que el padre no usa ninguno y si lo cerramos antes los hijos heredan descriptores cerrados cosa que daría fallos
    for ( i = 0; i < N; i++){
        close(pipes_vector[i][0]);
        close(pipes_vector[i][1]);
    }

    // como buen padre espera a todos sus hijos
    for (i = 0; i < N; i++){
        wait(NULL);
    }
    

    // pendiente hacer free de las pipes y sus descriptores

    // Liberar memoria al final
    for (i = 0; i < N - 1; i++) {
        free(pipes_vector[i]);
    }
    free(pipes_vector);
    free(pids_vector);
    return 0;
}

