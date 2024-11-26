





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