#include "sjf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "msg.h"

// Função auxiliar para retirar da fila o PCB com menor tempo total

// 1) Vamos tirar todos os elementos da fila original (rq) um a um.
// 2) Mantemos numa variável "shortest" o melhor (menor tempo) que encontrarmos até ao momento.
//    - Sempre que aparece um novo "mais curto", o antigo "shortest" vai para uma fila auxiliar.
//    - Se não for o mais curto, o elemento vai direto para a fila auxiliar.
// 3) No fim, devolvemos tudo da fila auxiliar para a fila original (menos o escolhido).
// 4) Retornamos o "shortest" para usar no CPU.
static pcb_t* dequeue_shortest_job(queue_t *rq) {
    // Se a fila estiver vazia, não há nada para escolher
    if (rq == NULL) {
        return NULL;
    }


    queue_t aux = (queue_t){0};

    pcb_t *shortest = NULL; // aqui vamos guardar o processo com menor tempo encontrado
    pcb_t *curr = NULL;     // variável temporária para ir recebendo cada elemento da fila

    // 1) Esvaziar a fila original, escolhendo o mais curto
    while ((curr = dequeue_pcb(rq)) != NULL) {
        // Se ainda não temos "shortest", o primeiro elemento passa a ser o mais curto por agora
        if (shortest == NULL) {
            shortest = curr;
            continue;
        }

        // 2) Comparar tempos, usamos o tempo total declarado (time_ms)
        if (curr->time_ms < shortest->time_ms) {
            // O "curr" é melhor (mais curto). O antigo "shortest" volta para a fila auxiliar
            enqueue_pcb(&aux, shortest);
            shortest = curr;
        } else {
            // O "curr" não é melhor, vai para a fila auxiliar
            enqueue_pcb(&aux, curr);
        }
    }

    // 3) Voltar a pôr na fila original todos os elementos que não foram escolhidos
    pcb_t *rest = NULL;
    while ((rest = dequeue_pcb(&aux)) != NULL) {
        enqueue_pcb(rq, rest);
    }

    // 4) Devolver o processo mais curto encontrado (ou NULL se a fila estivesse vazia)
    return shortest;
}

void sjf_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    if (*cpu_task) {
        // 1) Se existe tarefa no CPU, avançamos o "tempo decorrido" desta tarefa em um tick
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        // 2) Se a tarefa já cumpriu (ou ultrapassou) o seu tempo total, sinalizamos conclusão e libertamos o CPU
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            free(*cpu_task);
            *cpu_task = NULL;
        }
    }

    // 3) Se o CPU ficou livre, escolhemos da fila a tarefa mais curta
    if (*cpu_task == NULL) {
        *cpu_task = dequeue_shortest_job(rq); // única diferença para FIFO
    }
}