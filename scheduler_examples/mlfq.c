#include "mlfq.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "msg.h"

// - 3 filas internas: Q0 (mais prioritária), Q1, Q2
// - por nível (em ticks): Q0=1, Q1=2, Q2=4
// - Novas tarefas entram sempre em Q0
// - Se não termina no quantum, preempta e desce 1 nível (até Q2)
// - Boost simples: periodicamente, tudo em Q1/Q2 sobe para Q0 para evitar starvation
// 500 ms por time-slice = 50 ticks (TICKS_MS = 10 ms)
#define MLFQ_QUANTUM_TICKS 50

#define BOOST_PERIOD_MS 100

static queue_t Q0 = {0};
static queue_t Q1 = {0};
static queue_t Q2 = {0};

// Estado do CPU para controlar quantum do processo atual
static int current_level = 0;   // 0:Q0, 1:Q1, 2:Q2 (válido quando há tarefa no CPU)
static int current_q_used = 0;  // ticks já gastos no quantum atual
static uint32_t last_boost_ms = 0;


static inline void enqueue_level(pcb_t *p, int lvl) {
    if (lvl <= 0) {
        enqueue_pcb(&Q0, p);
    } else if (lvl == 1) {
        enqueue_pcb(&Q1, p);
    } else {
        enqueue_pcb(&Q2, p);
    }
}

static inline pcb_t* dequeue_next_by_priority(int *out_level) {
    pcb_t *p = dequeue_pcb(&Q0);
    if (p) { *out_level = 0; return p; }
    p = dequeue_pcb(&Q1);
    if (p) { *out_level = 1; return p; }
    p = dequeue_pcb(&Q2);
    if (p) { *out_level = 2; return p; }
    return NULL;
}

static void ingest_new_arrivals(queue_t *rq) {
    // Tudo o que chega do escalonador externo (ready_queue) vai para Q0
    pcb_t *p;
    while ((p = dequeue_pcb(rq)) != NULL) {
        enqueue_pcb(&Q0, p);
    }
}

static void maybe_boost(uint32_t now_ms) {
    if (now_ms - last_boost_ms < BOOST_PERIOD_MS) {
        return;
    }
    // Sobe tudo para Q0
    pcb_t *p;
    while ((p = dequeue_pcb(&Q1)) != NULL) { enqueue_pcb(&Q0, p); }
    while ((p = dequeue_pcb(&Q2)) != NULL) { enqueue_pcb(&Q0, p); }
    last_boost_ms = now_ms;
}

void mlfq_scheduler(uint32_t current_time_ms, queue_t *rq, queue_t *blocked_q, pcb_t **cpu_task) {

    // 1) Novas chegadas para Q0
    ingest_new_arrivals(rq);

    // 2) Boost periódico
    maybe_boost(current_time_ms);

    // 3) Se há tarefa no CPU, avança um tick e decide
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        current_q_used += 1;

        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // Terminou
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
            current_q_used = 0;
        } else {
            // Não terminou: se esgotou o quantum, preempta e desce 1 nível
            if (current_q_used >=  MLFQ_QUANTUM_TICKS) {
                int next_level = current_level < 2 ? current_level + 1 : 2;
                enqueue_level(*cpu_task, next_level);
                *cpu_task = NULL;
                current_q_used = 0;
            }
            // Caso contrário, mantém no CPU para o próximo tick
        }
    }

    // 4) Se o CPU está livre, escolhe pela prioridade: Q0 -> Q1 -> Q2
    if (*cpu_task == NULL) {
        *cpu_task = dequeue_next_by_priority(&current_level);
        current_q_used = 0;
    }
}