#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include "parser.h"



int child_number;                                                       // hacemos como en relevos y la i

int main(int argc, char const *argv[])
{
    
    int i;
    int N;                                                              // leer con tokenize
    char input[1024];
    fgets(input, sizeof(input), stdin);                                 // leemos la linea de comandos
    tline *line = tokenize(input);                                      // tokenizamos la linea
    N = line->ncommands;                                                // asignamos a N el numero de comandos

    //-----------------------------------------------------------------------------

    pid_t pid;
    int **pids_vector;                                                      // puntero al vector de pids
    pids_vector = (pid_t*)malloc(N * sizeof(pid_t));                        // reservamos memoria para n-1 pids
    
    //-----------------------------------------------------------------------------

    int **pipes_vector;
    pipes_vector = (int **)malloc((N-1)* sizeof(int));                        // reservamos memoria para N-1 mandatos

    // dentro de cada hueco para la pipe reservamos memoria para sus dos descriptores
    for (i = 0; i < N-1; i++) {
        pipes_vector[i] = (int *)malloc(2*sizeof(int));
    }

    // creamos las pipes 
    for ( i = 0; i < N; i++){
        pipe(pipes_vector[i]);
    }

    // ------------------------------------------------------------------------------

    // CREACION DE PROCESOS HIJOS Y LA DE DIOS
    int j;
    for (i = 0; i < N; i++){
        child_number = i;                                   // guardamos el id del hijo para tenerlo identificado despues de este for
        pid = fork();
        if (pid < 0){
            fprintf(stderr, "Error al crear proceso hijo");
        } else if (pid == 0) {
            
            if (i == 0){                                    // primer mandato
                close(pipes_vector[0][0]); 
                dup2(pipes_vector[0][1], STDOUT_FILENO);    // redirigimos su salida al extremo de escritura [1] de la primera pipe
                for ( j = 1; j < N-1; j++){                 // cerramos todos los descriptores de las pipes siguientes a la que usa
                    close(pipes_vector[j][0]);
                    close(pipes_vector[j][1]);
                }
            } else if ( i == N-1){                          // ultimo mandato
                close(pipes_vector[i][1]);
                dup2(pipes_vector[i][0], STDIN_FILENO);
                for ( j = i; j > 0; j--){
                    close(pipes_vector[j][0]);
                    close(pipes_vector[j][1]);
                }
                
            } else {                                        // mandato intermedio
                close(pipes_vector[i-1][1]);                // de la pipe anterior cierras el extremo de escritura es decir el 1
                close(pipes_vector[i][0]);                  // de la pipe siguiente cierras el de lectura el 0
                dup2(pipes_vector[i-1][0], STDIN_FILENO);
                dup2(pipes_vector[i][1], STDOUT_FILENO);

                // implementar el for que cierre todo el resto de las pipes PENSAR
                

            }

        } else {

             pids_vector[i] = pid;                  // nos guardamos el pid del hijo en su posición
            
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





/* 
-  funcion para crear pipes
    - dependiendo del numero de comandos crea n-1 pipes
- enlace de pipes y creacion de hijos

    for 0 to len arraymandatos do 
        pid = fork()
        array pids.push pid 
        if fallo 
        else if == 0 hijo
            if i == 0 -> mandato inicial
                cerrar pipe 0 por [0]
            else if i == len arraymandatos -> final
                cerrar pipe n-1 por [1] (en caso de que i llegue hasta n-1)
            else -> intermedios 
                cerrar la pipe de la izquierda por [1] pipe por la que lee
                cerrar la pope de la derecha por [0] pipe por la que escribe
        else 
            wait






*/