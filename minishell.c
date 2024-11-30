#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include "parser.h"
#include <signal.h>

#define MAX_COMMANDS 20

// ---------------------------------------------------------------------------------DEFINIMOS ESTRUCTURA TLINE
// typedef struct
// {
//     char **tokens; // array de punteros a tokens
//     int ncommands; // numero de comandos en la linea
// } tline;

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
int *create_pids_vector(pid_t pid, int N)
{
    int i;
    int **pids_vector = (pid_t *)malloc(N * sizeof(pid_t)); // reservamos memoria para n-1 pids

    return pids_vector;
}

// ---------------------------------------------------------------------------------CREAR ARRAY DE PIPES
int *create_pipes_vector(int N)
{
    int i;
    int **pipes_vector;
    pipes_vector = (int **)malloc((N - 1) * sizeof(int)); // reservamos memoria para N-1 mandatos

    // dentro de cada hueco para la pipe reservamos memoria para sus dos descriptores
    for (i = 0; i < N - 1; i++)
    {
        pipes_vector[i] = (int *)malloc(2 * sizeof(int));
    }

    // creamos las pipes
    for (i = 0; i < N; i++)
    {
        pipe(pipes_vector[i]);
    }

    return pipes_vector;
}

int j;

// ---------------------------------------------------------------------------------FUNCIÓN MAIN
int main(int argc, char const *argv[])
{
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    int i;
    char input[1024];
    fgets(input, sizeof(input), stdin);

    // Verificar si la entrada está vacía
    if (input[0] == '\n')
    {
        printf("Entrada vacía. Saliendo...\n");
        return 0;
    }

    // Eliminar espacios y saltos de línea al final de la entrada
    size_t len = strlen(input);
    while (len > 0 && (input[len - 1] == ' ' || input[len - 1] == '\n'))
    {
        input[len - 1] = '\0'; // Elimino el caracter
        len--;
    }
    // esto creo q no es necesario lo de la funcion porq lo hace directamente la libreria creo 
    //tline *line = tokenize_input(input);
    tline *line = tokenize(input); 
    int N = line->ncommands;

    //-----------------------------------------------------------------------------

    pid_t pid;
    int **pids_vector = create_pids_vector(pid, N); // puntero al vector de pids
    int **pipes_vector = create_pipes_vector(N);

    // ------------------------------------------------------------------------------

    // CREACION DE PROCESOS HIJOS Y LA DE DIOS

    for (i = 0; i < N; i++){
        child_number = i;                                   // guardamos el id del hijo para tenerlo identificado despues de este for
        pid = fork();
        if (pid < 0){
            fprintf(stderr, "Error al crear proceso hijo \n");
        } else if (pid == 0) {
            
            if (i == 0){                                    // primer mandato
                close(pipes_vector[0][0]); 
                dup2(pipes_vector[0][1], STDOUT_FILENO);    // redirigimos su salida al extremo de escritura [1] de la primera pipe
                for ( j = 1; j < N-1; j++){                 // cerramos todos los descriptores de las pipes siguientes a la que usa
                    close(pipes_vector[j][0]);
                    close(pipes_vector[j][1]);
                }
            } else if ( i == N-1){                          // ultimo mandato
                close(pipes_vector[i-1][1]);
                dup2(pipes_vector[i-1][0], STDIN_FILENO);
                for ( j = i; j > 0; j--){
                    close(pipes_vector[j-2][0]);
                    close(pipes_vector[j-2][1]);
                }
                
            } else {                                        // mandato intermedio
                /* 

            [0]         [1]         [2]         [3]         [4]         [5]         [6]         [7]
                =======     =======     =======     =======     =======     =======     =======    
                    0           1           2           3           4           5           6

            */
                close(pipes_vector[i-1][1]);                // de la pipe anterior cierras el extremo de escritura es decir el 1
                close(pipes_vector[i][0]);                  // de la pipe siguiente cierras el de lectura el 0
                dup2(pipes_vector[i-1][0], STDIN_FILENO);
                dup2(pipes_vector[i][1], STDOUT_FILENO);

                // cerramos las pipes que no utilizamos hasta el inicio
                for ( j = i; j >= 0 ; j--){
                    close(pipes_vector[j-2][1]);            // cerramos las pipes que no usa 
                    close(pipes_vector[j-2][0]); 
                }

                // cerramos las pipes que no utilizamos hasta el final
                for (j = i; i < N; j++){
                    close(pipes_vector[j+2][1]);
                    close(pipes_vector[j+2][0]);
                }
            }
            printf("Todo cerrado y redireccionado con exito vamos con el exec de: %s \n", line->commands[i].filename);
            execvp(line->commands[i].filename, line->commands[i].argv);

        } else {

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
    return 0;
}

