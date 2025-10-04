#include "rr.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "msg.h"

// Quantum de 500 ms = 50 ticks (TICKS_MS = 10 ms)
#define RR_QUANTUM_TICKS 50
static int rr_ticks_used = 0; // contador do slice atual

// - Quantum = 1 TICK (TICKS_MS)
// - Em cada chamada: avança 1 tick da tarefa em CPU.
//   * Se terminou: envia DONE e liberta.
//   * Se não terminou: re-enfila no fim da ready queue e liberta o CPU.
// - Depois, se o CPU estiver livre, vai buscar a próxima da fila (como no FIFO).
void rr_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    if (*cpu_task) {
        // 1) Avançar 1 tick do trabalho da tarefa atual
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        rr_ticks_used += 1; // um tick consumido no quantum atual

        // 2) Verificar se a tarefa terminou neste tick
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            // Terminou: libertar PCB e CPU
            free(*cpu_task);
            *cpu_task = NULL;
        } else {
            // 3) Não terminou ainda; só preempta quando atingir 500 ms (50 ticks)
            if (rr_ticks_used >= RR_QUANTUM_TICKS) {
                // atingiu 500 ms de CPU → re-enfileirar e libertar o CPU
                enqueue_pcb(rq, *cpu_task);
                *cpu_task = NULL;
                rr_ticks_used = 0; // reset do slice
            }
            // caso contrário mantém-se no CPU para o próximo tick
        }
    }

    // 4) Se o CPU ficou livre, buscar próxima tarefa da fila (igual ao FIFO)
    if (*cpu_task == NULL) {
        *cpu_task = dequeue_pcb(rq);
        rr_ticks_used = 0; // novo processo no CPU → reinicia contador de slice
    }
}