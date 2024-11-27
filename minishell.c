#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>





int main(int argc, char const *argv[])
{
    
    int i;
    int N = 30;                                                             // leer con tokenize

    pid_t pid;
    int **pids_vector;                                                      // puntero al vector de pids
    pids_vector = (int *)malloc((N-1) * sizeof(int));                     // reservamos memoria para n-1 pids
    for (i = 0; i < N; i++){
        pids_vector[i] = (pid_t *)malloc(sizeof(pid_t));
    }
    

    int **pipes_vector;
    pipes_vector = (int **)malloc((N-1)* sizeof(int));                        // reservamos memoria para N-1 mandatos



    // dentro de cada hueco para la pipe reservamos memoria para sus dos descriptores
    for (i = 0; i < N; i++) {
        pipes_vector[i] = (int *)malloc(2*sizeof(int));
    }

    // creamos las pipes 
    for ( i = 0; i < N; i++){
        pipe(pipes_vector[i]);
    }

    // ------------------------------------------------------------------------------

    // CREACION DE PROCESOS HIJOS Y LA DE DIOS
    for (i = 0; i < N; i++){
        pid = fork();

        if (pid < 0){
            fprintf(stderr, "Error al crear proceso hijo");
        } else if (pid == 0) {
            // implementar logica

        } else {

            // cerramos todos los descriptores de los pipes ya que el padre no usa ninguno
            for ( i = 0; i < N; i++){
                close(pipes_vector[i][0]);
                close(pipes_vector[i][1]);
            }

            // como buen padre espera a todos sus hijos
            for (i = 0; i < N; i++){
                wait(NULL);
            }
            
        }
        
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