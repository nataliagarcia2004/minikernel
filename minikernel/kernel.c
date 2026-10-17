/*
 *  kernel/kernel.c
 *
 *  Minikernel. Versi�n 1.0
 *
 *  Fernando P�rez Costoya
 *
 */

/*
 *
 * Fichero que contiene la funcionalidad del sistema operativo
 *
 */

#include "kernel.h"	/* Contiene defs. usadas por este modulo */
#include <string.h>
/*
 *
 * Funciones relacionadas con la tabla de procesos:
 *	iniciar_tabla_proc buscar_BCP_libre
 *
 */

/*
 * Funci�n que inicia la tabla de procesos
 */
static void iniciar_tabla_proc(){
	int i;

	for (i=0; i<MAX_PROC; i++)
		tabla_procs[i].estado=NO_USADA;
}

/*
 * Inicializa la tabla de mutex
 */
static void iniciar_tabla_mutex(){
    int i;

    for(i=0; i<NUM_MUT; i++){
        tabla_mutex[i].usado = MUTEX_NO_USADO;
        tabla_mutex[i].nombre[0] = '\0';
        tabla_mutex[i].tipo = NO_RECURSIVO;
        tabla_mutex[i].num_abiertos = 0;
		tabla_mutex[i].propietario = NULL;
		tabla_mutex[i].num_bloqueos = 0;
		tabla_mutex[i].espera_lock.primero = NULL;
		tabla_mutex[i].espera_lock.ultimo = NULL;
    }
}

/*
 * Funci�n que busca una entrada libre en la tabla de procesos
 */
static int buscar_BCP_libre(){
	int i;

	for (i=0; i<MAX_PROC; i++)
		if (tabla_procs[i].estado==NO_USADA)
			return i;
	return -1;
}

/*
 * Busca un mutex por su nombre
 * Devuelve su posicion en tabla_mutex o -1 si no existe
 */
static int buscar_mutex_por_nombre(char *nombre){
    int i;

    for(i=0; i<NUM_MUT; i++){

        if(tabla_mutex[i].usado == MUTEX_USADO &&
           strcmp(tabla_mutex[i].nombre, nombre) == 0){

            return i;
        }
    }

    return -1;
}

/*
 * Busca un descriptor libre en el proceso actual.
 * Devuelve su posicion o -1 si no hay ninguno libre.
 */
static int buscar_descriptor_libre(){
    int i;

    for(i=0; i<NUM_MUT_PROC; i++){

        if(p_proc_actual->descriptores[i] == -1){
            return i;
        }
    }

    return -1;
}

/*
 * Busca una posicion libre en la tabla global de mutex.
 * Devuelve la posicion o -1 si la tabla esta llena.
 */
static int buscar_hueco_mutex(){
    int i;

    for(i=0; i<NUM_MUT; i++){

        if(tabla_mutex[i].usado == MUTEX_NO_USADO){
            return i;
        }
    }

    return -1;
}

/*
 *
 * Funciones que facilitan el manejo de las listas de BCPs
 *	insertar_ultimo eliminar_primero eliminar_elem
 *
 * NOTA: PRIMERO SE DEBE LLAMAR A eliminar Y LUEGO A insertar
 */

/*
 * Inserta un BCP al final de la lista.
 */
static void insertar_ultimo(lista_BCPs *lista, BCP * proc){
	if (lista->primero==NULL)
		lista->primero= proc;
	else
		lista->ultimo->siguiente=proc;
	lista->ultimo= proc;
	proc->siguiente=NULL;
}

/*
 * Elimina el primer BCP de la lista.
 */
static void eliminar_primero(lista_BCPs *lista){

	if (lista->ultimo==lista->primero)
		lista->ultimo=NULL;
	lista->primero=lista->primero->siguiente;
}

/*
 * Elimina un determinado BCP de la lista.
 */
static void eliminar_elem(lista_BCPs *lista, BCP * proc){
	BCP *paux=lista->primero;

	if (paux==proc)
		eliminar_primero(lista);
	else {
		for ( ; ((paux) && (paux->siguiente!=proc));
			paux=paux->siguiente);
		if (paux) {
			if (lista->ultimo==paux->siguiente)
				lista->ultimo=paux;
			paux->siguiente=paux->siguiente->siguiente;
		}
	}
}

/*
 *
 * Funciones relacionadas con la planificacion
 *	espera_int planificador
 */

/*
 * Espera a que se produzca una interrupcion
 */
static void espera_int(){
	int nivel;

	printk("-> NO HAY LISTOS. ESPERA INT\n");

	/* Baja al m�nimo el nivel de interrupci�n mientras espera */
	nivel=fijar_nivel_int(NIVEL_1);
	halt();
	fijar_nivel_int(nivel);
}

/*
 * Funci�n de planificacion que implementa un algoritmo FIFO.
 */
static BCP * planificador(){
	while (lista_listos.primero==NULL)
		espera_int();		/* No hay nada que hacer */
	return lista_listos.primero;
}

static int cerrar_mutex_descriptor(unsigned int mutexid){

    int posicion_mutex;
    int nivel_anterior;
    BCP *p_proc_desbloqueado;
    BCP *p_proc_creador;

    /* Comprueba que el descriptor este dentro de los posibles del proceso */
    if(mutexid >= NUM_MUT_PROC){
        return -1;
    }

    /* Obtiene la posicion del mutex en la tabla global */
    posicion_mutex = p_proc_actual->descriptores[mutexid];

    /* Comprueba que el descriptor este realmente abierto */
    if(posicion_mutex < 0 || posicion_mutex >= NUM_MUT){
        return -1;
    }

    /* Comprueba que el mutex siga existiendo */
    if(tabla_mutex[posicion_mutex].usado != MUTEX_USADO){
        return -1;
    }

    /* ZONA CRITICA: se va a modificar el mutex y los descriptores */
    nivel_anterior = fijar_nivel_int(NIVEL_3);

    /* Si el proceso actual es propietario, libera completamente el mutex */
    if(tabla_mutex[posicion_mutex].propietario == p_proc_actual){

        /* Si nadie espera, el mutex queda libre */
        if(tabla_mutex[posicion_mutex].espera_lock.primero == NULL){

            tabla_mutex[posicion_mutex].propietario = NULL;
            tabla_mutex[posicion_mutex].num_bloqueos = 0;
        }

        /* Si hay alguien esperando, el mutex pasa al primero */
        else{

            p_proc_desbloqueado =
                tabla_mutex[posicion_mutex].espera_lock.primero;

            eliminar_primero(&tabla_mutex[posicion_mutex].espera_lock);

            tabla_mutex[posicion_mutex].propietario = p_proc_desbloqueado;
            tabla_mutex[posicion_mutex].num_bloqueos = 1;

            p_proc_desbloqueado->estado = LISTO;

            insertar_ultimo(&lista_listos, p_proc_desbloqueado);
        }
    }

    /* Cierra el descriptor del proceso */
    p_proc_actual->descriptores[mutexid] = -1;

    /* Hay una referencia menos al mutex */
    tabla_mutex[posicion_mutex].num_abiertos--;

    /* Si otros procesos todavia lo tienen abierto, el mutex sigue existiendo */
    if(tabla_mutex[posicion_mutex].num_abiertos > 0){

        fijar_nivel_int(nivel_anterior);
        return 0;
    }

    /* Nadie tiene ya abierto el mutex: se elimina de la tabla global */
    tabla_mutex[posicion_mutex].usado = MUTEX_NO_USADO;
    tabla_mutex[posicion_mutex].nombre[0] = '\0';
    tabla_mutex[posicion_mutex].tipo = NO_RECURSIVO;
    tabla_mutex[posicion_mutex].num_abiertos = 0;
    tabla_mutex[posicion_mutex].propietario = NULL;
    tabla_mutex[posicion_mutex].num_bloqueos = 0;
    tabla_mutex[posicion_mutex].espera_lock.primero = NULL;
    tabla_mutex[posicion_mutex].espera_lock.ultimo = NULL;

    /* Si hay un proceso esperando para crear un mutex, se despierta */
    if(lista_espera_crear_mutex.primero != NULL){

        p_proc_creador = lista_espera_crear_mutex.primero;

        eliminar_primero(&lista_espera_crear_mutex);

        p_proc_creador->estado = LISTO;

        insertar_ultimo(&lista_listos, p_proc_creador);
    }

    fijar_nivel_int(nivel_anterior);

    return 0;
}

/*
 *
 * Funcion auxiliar que termina proceso actual liberando sus recursos.
 * Usada por llamada terminar_proceso y por rutinas que tratan excepciones
 *
 */
static void liberar_proceso(){
	BCP * p_proc_anterior;
	int nivel_anterior;
	int i;

	/* Cierra todos los mutex que el proceso tenga abiertos */
    for(i=0; i<NUM_MUT_PROC; i++){

        if(p_proc_actual->descriptores[i] != -1){
            cerrar_mutex_descriptor(i);
        }
    }

	liberar_imagen(p_proc_actual->info_mem); /* liberar mapa */
	nivel_anterior = fijar_nivel_int(NIVEL_3);

	p_proc_actual->estado=TERMINADO;
	eliminar_primero(&lista_listos); /* proc. fuera de listos */

	fijar_nivel_int(nivel_anterior);
	
	/* Realizar cambio de contexto */
	p_proc_anterior=p_proc_actual;
	p_proc_actual=planificador();

	printk("-> C.CONTEXTO POR FIN: de %d a %d\n",
			p_proc_anterior->id, p_proc_actual->id);

	liberar_pila(p_proc_anterior->pila);
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
        return; /* no deber�a llegar aqui */
}

/*
 *
 * Funciones relacionadas con el tratamiento de interrupciones
 *	excepciones: exc_arit exc_mem
 *	interrupciones de reloj: int_reloj
 *	interrupciones del terminal: int_terminal
 *	llamadas al sistemas: llam_sis
 *	interrupciones SW: int_sw
 *
 */

/*
 * Tratamiento de excepciones aritmeticas
 */
static void exc_arit(){

	if (!viene_de_modo_usuario())
		panico("excepcion aritmetica cuando estaba dentro del kernel");


	printk("-> EXCEPCION ARITMETICA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no deber�a llegar aqui */
}

/*
 * Tratamiento de excepciones en el acceso a memoria
 */
static void exc_mem(){

	if (!viene_de_modo_usuario())
		panico("excepcion de memoria cuando estaba dentro del kernel");


	printk("-> EXCEPCION DE MEMORIA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no deber�a llegar aqui */
}

/*
 * Tratamiento de interrupciones de terminal
 */
static void int_terminal(){
	char car;

	car = leer_puerto(DIR_TERMINAL);
	printk("-> TRATANDO INT. DE TERMINAL %c\n", car);

        return;
}

static void actualizar_dormidos(){
    BCP *auxiliar;
    BCP *siguiente;

    auxiliar = lista_dormidos.primero;

    while(auxiliar != NULL){

        siguiente = auxiliar->siguiente;

        if(auxiliar->ticks_dormir > 0){
            auxiliar->ticks_dormir--;
        }

        if(auxiliar->ticks_dormir == 0){

            auxiliar->estado = LISTO;

            eliminar_elem(&lista_dormidos, auxiliar);
            insertar_ultimo(&lista_listos, auxiliar);
        }

        auxiliar = siguiente;
    }
}

/*
 * Tratamiento de interrupciones de reloj
 */
static void int_reloj(){

	printk("-> TRATANDO INT. DE RELOJ\n");
	actualizar_dormidos();
    return;
}

/*
 * Tratamiento de llamadas al sistema
 */
static void tratar_llamsis(){
	int nserv, res;

	nserv=leer_registro(0);
	if (nserv<NSERVICIOS)
		res=(tabla_servicios[nserv].fservicio)();
	else
		res=-1;		/* servicio no existente */
	escribir_registro(0,res);
	return;
}

/*
 * Tratamiento de interrupciuones software
 */
static void int_sw(){

	printk("-> TRATANDO INT. SW\n");

	return;
}

/*
 *
 * Funcion auxiliar que crea un proceso reservando sus recursos.
 * Usada por llamada crear_proceso.
 *
 */
static int crear_tarea(char *prog){
	void * imagen, *pc_inicial;
	int error=0;
	int proc, i;
	BCP *p_proc;
	int nivel_anterior;

	proc=buscar_BCP_libre();
	if (proc==-1)
		return -1;	/* no hay entrada libre */

	/* A rellenar el BCP ... */
	p_proc=&(tabla_procs[proc]);

	/* crea la imagen de memoria leyendo ejecutable */
	imagen=crear_imagen(prog, &pc_inicial);
	if (imagen)
	{
		p_proc->info_mem=imagen;
		p_proc->pila=crear_pila(TAM_PILA);
		fijar_contexto_ini(p_proc->info_mem, p_proc->pila, TAM_PILA,
			pc_inicial,
			&(p_proc->contexto_regs));
		p_proc->id=proc;
		p_proc->estado=LISTO;
		p_proc->ticks_dormir = 0;
		/* Inicializa los descriptores de mutex del proceso */
		for(i=0; i<NUM_MUT_PROC; i++){
			p_proc->descriptores[i] = -1;
		}

		nivel_anterior = fijar_nivel_int(NIVEL_3);
		/* lo inserta al final de cola de listos */
		insertar_ultimo(&lista_listos, p_proc);
		fijar_nivel_int(nivel_anterior);
		error= 0;
	}
	else
		error= -1; /* fallo al crear imagen */

	return error;
}

/*
 *
 * Rutinas que llevan a cabo las llamadas al sistema
 *	sis_crear_proceso sis_escribir
 *
 */

/*
 * Tratamiento de llamada al sistema crear_proceso. Llama a la
 * funcion auxiliar crear_tarea sis_terminar_proceso
 */
int sis_crear_proceso(){
	char *prog;
	int res;

	printk("-> PROC %d: CREAR PROCESO\n", p_proc_actual->id);
	prog=(char *)leer_registro(1);
	res=crear_tarea(prog);
	return res;
}

/*
 * Tratamiento de llamada al sistema escribir. Llama simplemente a la
 * funcion de apoyo escribir_ker
 */
int sis_escribir()
{
	char *texto;
	unsigned int longi;

	texto=(char *)leer_registro(1);
	longi=(unsigned int)leer_registro(2);

	escribir_ker(texto, longi);
	return 0;
}

/*
 * Tratamiento de llamada al sistema terminar_proceso. Llama a la
 * funcion auxiliar liberar_proceso
 */
int sis_terminar_proceso(){

	printk("-> FIN PROCESO %d\n", p_proc_actual->id);

	liberar_proceso();

        return 0; /* no deber�a llegar aqui */
}

/*
 * Tratamiento de llamada al sistema obtener_id_pr
 */
int sis_obtener_id_pr(){
    return p_proc_actual->id;
}

/*
 * Tratamiento de llamada al sistema dormir
 */
int sis_dormir(){
    unsigned int segundos;
    BCP *p_proc_dormido;
	int nivel_anterior;

    segundos = (unsigned int) leer_registro(1);

    p_proc_dormido = p_proc_actual;

	nivel_anterior = fijar_nivel_int(NIVEL_3);

	/*ZONA CRÍTICA: parte del código donde tocamos datos compartidos y no queremos que alguien nos interrumpa y los cambie a la vez. */
    p_proc_dormido->ticks_dormir = segundos * TICK;
    p_proc_dormido->estado = BLOQUEADO;

    eliminar_primero(&lista_listos);
    insertar_ultimo(&lista_dormidos, p_proc_dormido);

	fijar_nivel_int(nivel_anterior);

    p_proc_actual = planificador();
	
    cambio_contexto(&(p_proc_dormido->contexto_regs),
                    &(p_proc_actual->contexto_regs));

    return 0;
}

/*
 * Tratamiento de llamada al sistema crear_mutex
 */
int sis_crear_mutex(){
	char *nombre;
    int tipo;
	int descriptor; /*posición dentro de los 4 descriptores DEL PROCESO*/
	int posicion_mutex; /*posición dentro de los 16 mutex DEL SISTEMA*/
	BCP *p_proc_bloqueado;
	int nivel_anterior;

    nombre = (char *) leer_registro(1);
    tipo = (int) leer_registro(2);

	/* Comprueba que el tipo sea valido */
    if(tipo != NO_RECURSIVO && tipo != RECURSIVO){
        return -1;
    }

    /* Comprueba que el nombre quepa */
    if(strlen(nombre) >= MAX_NOM_MUT){
        return -1;
    }

	 /* Comprueba que no exista ya un mutex con ese nombre */
    if(buscar_mutex_por_nombre(nombre) != -1){
        return -1;
    }

	/* Busca un descriptor libre para el proceso */
	descriptor = buscar_descriptor_libre();

	if(descriptor == -1){
		return -1;
	}

	/* Busca un hueco libre en la tabla global de mutex */
	posicion_mutex = buscar_hueco_mutex();

	/*
     * Si no hay hueco en la tabla global,
     * el proceso se bloquea hasta que se elimine algun mutex.
     */
    while(posicion_mutex == -1){

        p_proc_bloqueado = p_proc_actual; /*Proceso que ejecuta crear mutex*/

         /*ZONA CRÍTICA: parte del código donde tocamos datos compartidos y no queremos 
		 que alguien nos interrumpa y los cambie a la vez. */
		nivel_anterior = fijar_nivel_int(NIVEL_3);
		/*bloqueamos el proceso qu elo ejecuta ya que no hay hueco*/
        p_proc_bloqueado->estado = BLOQUEADO; 

        eliminar_primero(&lista_listos); /*elminas ese proceso de la lista listos*/
        insertar_ultimo(&lista_espera_crear_mutex,
                        p_proc_bloqueado); /*y lo insertas en la lista de espera*/

        fijar_nivel_int(nivel_anterior); /*fin de la zona crítica*/

        /* Ejecuta otro proceso */
        p_proc_actual = planificador();

        cambio_contexto(&(p_proc_bloqueado->contexto_regs),
                        &(p_proc_actual->contexto_regs));


        /*
         * Cuando este proceso vuelva a ejecutarse,
         * hay que comprobar otra vez si alguien ha creado
         * mientras tanto un mutex con el mismo nombre.
         */
        if(buscar_mutex_por_nombre(nombre) != -1){
            return -1;
        }

        /*
         * Tambien se vuelve a comprobar si realmente
         * hay un hueco libre.
         */
        posicion_mutex = buscar_hueco_mutex();
    }


    /*
     * Si hemos llegado aqui, existe un hueco libre.
     * Creamos el mutex.
     */
    strcpy(tabla_mutex[posicion_mutex].nombre, nombre);

    tabla_mutex[posicion_mutex].tipo = tipo;
    tabla_mutex[posicion_mutex].usado = MUTEX_USADO;
    tabla_mutex[posicion_mutex].num_abiertos = 1;
	tabla_mutex[posicion_mutex].propietario = NULL;
	tabla_mutex[posicion_mutex].num_bloqueos = 0;
	tabla_mutex[posicion_mutex].espera_lock.primero = NULL;
	tabla_mutex[posicion_mutex].espera_lock.ultimo = NULL;


    /*
     * El descriptor del proceso apunta a la posicion
     * del mutex en la tabla global.
     */
    p_proc_actual->descriptores[descriptor] = posicion_mutex;


    /* crear_mutex devuelve el descriptor local del proceso */
    return descriptor;
}


/*
 * Tratamiento de llamada al sistema abrir_mutex
 */
int sis_abrir_mutex(){

    char *nombre;
    int posicion_mutex;
    int descriptor;

    /* Lee el nombre enviado por el programa de usuario */
    nombre = (char *) leer_registro(1);

    /* Busca el mutex que se quiere abrir */
    posicion_mutex = buscar_mutex_por_nombre(nombre);

    /* Si no existe, error */
    if(posicion_mutex == -1){
        return -1;
    }

    /* Busca un descriptor libre para el proceso */
    descriptor = buscar_descriptor_libre();

    /* Si no quedan descriptores, error */
    if(descriptor == -1){
        return -1;
    }

    /* Asocia el descriptor local con el mutex global */
    p_proc_actual->descriptores[descriptor] = posicion_mutex;

    /* Hay un descriptor mas asociado a este mutex */
    tabla_mutex[posicion_mutex].num_abiertos++;

    /* Devuelve el descriptor local */
    return descriptor;
}

int sis_lock(){
    unsigned int mutexid;
	int posicion_mutex;
	int nivel_anterior;
	BCP *p_proc_bloqueado;

	/* Lee el descriptor local enviado por el programa de usuario */
    mutexid = (unsigned int) leer_registro(1);
	 /* Comprueba que el descriptor este dentro de los posibles del proceso */
    if(mutexid >= NUM_MUT_PROC){
        return -1;
	}
	/* Obtiene la posicion del mutex en la tabla global */
    posicion_mutex = p_proc_actual->descriptores[mutexid];

	/* Comprueba que el descriptor este realmente abierto */
	if(posicion_mutex < 0 || posicion_mutex >= NUM_MUT){
		return -1;
	}
	/* Comprueba que el mutex siga existiendo en la tabla global */
	if(tabla_mutex[posicion_mutex].usado != MUTEX_USADO){
		return -1;
	}

	/* ZONA CRITICA: a partir de aqui se modifica el estado del mutex */
	nivel_anterior = fijar_nivel_int(NIVEL_3);

	/* CASO 1: el mutex esta libre */
	if(tabla_mutex[posicion_mutex].propietario == NULL){

		tabla_mutex[posicion_mutex].propietario = p_proc_actual;
		tabla_mutex[posicion_mutex].num_bloqueos = 1;

		fijar_nivel_int(nivel_anterior);

		return 0;
	}

	/* CASO 2:NO LIBRE Y el mutex ya pertenece al proceso actual */
	if(tabla_mutex[posicion_mutex].propietario == p_proc_actual){

		/* Si es NO_RECURSIVO, no puede volver a bloquearlo */
		if(tabla_mutex[posicion_mutex].tipo == NO_RECURSIVO){

			fijar_nivel_int(nivel_anterior);
			return -1;
		}

		/* Si es RECURSIVO, puede volver a bloquearlo */
		tabla_mutex[posicion_mutex].num_bloqueos++;

		fijar_nivel_int(nivel_anterior);
		return 0;
	}

	/* CASO 3: el mutex pertenece a otro proceso */
	p_proc_bloqueado = p_proc_actual;

	/* El proceso actual queda bloqueado */
	p_proc_bloqueado->estado = BLOQUEADO;

	/* Sale de listos y espera especificamente por este mutex */
	eliminar_primero(&lista_listos);
	insertar_ultimo(&tabla_mutex[posicion_mutex].espera_lock,
					p_proc_bloqueado);

	fijar_nivel_int(nivel_anterior);
	p_proc_actual = planificador();
	/* Cambia del proceso bloqueado al nuevo proceso elegido */
	cambio_contexto(&(p_proc_bloqueado->contexto_regs),
					&(p_proc_actual->contexto_regs));

	return 0;

}

int sis_unlock(){

    unsigned int mutexid;
    int posicion_mutex;
    int nivel_anterior;
	BCP *p_proc_desbloqueado;

    /* Lee el descriptor local enviado por el programa de usuario */
    mutexid = (unsigned int) leer_registro(1);

    /* Comprueba que el descriptor este dentro de los posibles del proceso */
    if(mutexid >= NUM_MUT_PROC){
        return -1;
    }

    /* Obtiene la posicion del mutex en la tabla global */
    posicion_mutex = p_proc_actual->descriptores[mutexid];

    /* Comprueba que el descriptor este realmente abierto */
    if(posicion_mutex < 0 || posicion_mutex >= NUM_MUT){
        return -1;
    }

    /* Comprueba que el mutex siga existiendo */
    if(tabla_mutex[posicion_mutex].usado != MUTEX_USADO){
        return -1;
    }

    /* ZONA CRITICA: se va a modificar el estado del mutex */
    nivel_anterior = fijar_nivel_int(NIVEL_3);

    /* Solo el propietario puede hacer unlock */
    if(tabla_mutex[posicion_mutex].propietario != p_proc_actual){

        fijar_nivel_int(nivel_anterior);
        return -1;
    }

	/* Reduce en uno el numero de bloqueos */
	tabla_mutex[posicion_mutex].num_bloqueos--;

	/* Si quedan bloqueos pendientes, el mutex sigue perteneciendo al proceso */
	if(tabla_mutex[posicion_mutex].num_bloqueos > 0){

		fijar_nivel_int(nivel_anterior);
		return 0;
	}

	/* Si no hay procesos esperando, el mutex queda libre */
	if(tabla_mutex[posicion_mutex].espera_lock.primero == NULL){

		tabla_mutex[posicion_mutex].propietario = NULL;

		fijar_nivel_int(nivel_anterior);
		return 0;
	}
	/* Hay un proceso esperando: cogemos el primero */
	p_proc_desbloqueado = tabla_mutex[posicion_mutex].espera_lock.primero;

	/* Lo elimina de la cola de espera del mutex */
	eliminar_primero(&tabla_mutex[posicion_mutex].espera_lock);

	/* El proceso despertado pasa a ser el nuevo propietario */
	tabla_mutex[posicion_mutex].propietario = p_proc_desbloqueado;
	tabla_mutex[posicion_mutex].num_bloqueos = 1;

	/* El proceso deja de estar bloqueado */
	p_proc_desbloqueado->estado = LISTO;
	
	/* Lo introduce en la lista de procesos listos */
	insertar_ultimo(&lista_listos, p_proc_desbloqueado);

	/* Fin de la zona critica */
	fijar_nivel_int(nivel_anterior);

	return 0;

}

int sis_cerrar_mutex(){

    unsigned int mutexid;

    /* Lee el descriptor local enviado por el programa de usuario */
    mutexid = (unsigned int) leer_registro(1);

    /* Realiza el cierre del mutex */
    return cerrar_mutex_descriptor(mutexid);
}


/*
 *
 * Rutina de inicializaci�n invocada en arranque
 *
 */
int main(){
	/* se llega con las interrupciones prohibidas */

	instal_man_int(EXC_ARITM, exc_arit); 
	instal_man_int(EXC_MEM, exc_mem); 
	instal_man_int(INT_RELOJ, int_reloj); 
	instal_man_int(INT_TERMINAL, int_terminal); 
	instal_man_int(LLAM_SIS, tratar_llamsis); 
	instal_man_int(INT_SW, int_sw); 

	iniciar_cont_int();		/* inicia cont. interr. */
	iniciar_cont_reloj(TICK);	/* fija frecuencia del reloj */
	iniciar_cont_teclado();		/* inici cont. teclado */

	iniciar_tabla_proc();		/* inicia BCPs de tabla de procesos */
	iniciar_tabla_mutex();		/* inicia tabla de mutex */

	/* crea proceso inicial */
	if (crear_tarea((void *)"init")<0)
		panico("no encontrado el proceso inicial");

	
	/* activa proceso inicial */
	p_proc_actual=planificador();
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	panico("S.O. reactivado inesperadamente");
	return 0;
}
