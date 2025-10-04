#include "mlfq.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "msg.h"

/*
 * Implementação simples de MLFQ (3 níveis) usando apenas este ficheiro.
 * Regras:
 *  - Q0 (mais prioritária): quantum = 1 tick  (10 ms)
 *  - Q1:                      quantum = 2 ticks (20 ms)
 *  - Q2:                      quantum = 4 ticks (40 ms)
 *  - Tarefas novas entram sempre em Q0.
 *  - Se esgotarem o quantum sem terminar → preempt e descem um nível (até Q2).
 *  - Boost periódico: a cada 2000 ms promovemos Q1/Q2 para Q0.
 *  - O blocked_q é gerido no ossim.c; aqui não precisamos de o usar.
 */

#define Q0_QUANTUM_TICKS 1
#define Q1_QUANTUM_TICKS 2
#define Q2_QUANTUM_TICKS 4

#define BOOST_PERIOD_MS 2000

typedef struct level_entry_st {
    int pid;
    int level; // 0,1,2
    struct level_entry_st* next;
} level_entry_t;

static queue_t Q0 = { .head = NULL, .tail = NULL };
static queue_t Q1 = { .head = NULL, .tail = NULL };
static queue_t Q2 = { .head = NULL, .tail = NULL };

static level_entry_t* levels = NULL;     // pid→level
static int current_slice_ticks = 0;      // ticks gastos pela tarefa atual
static uint32_t last_boost_time_ms = 0;  // último boost

// --- helpers pid→level ---
static int get_level_for_pid(int pid) {
    level_entry_t* it = levels;
    while (it) {
        if (it->pid == pid) return it->level;
        it = it->next;
    }
    return 0; // por omissão, novos entram em Q0
}

static void set_level_for_pid(int pid, int level) {
    level_entry_t* it = levels;
    while (it) {
        if (it->pid == pid) { it->level = level; return; }
        it = it->next;
    }
    level_entry_t* node = (level_entry_t*)malloc(sizeof(level_entry_t));
    if (!node) return;
    node->pid = pid;
    node->level = level;
    node->next = levels;
    levels = node;
}

static int quantum_for_level(int level) {
    switch (level) {
        case 0: return Q0_QUANTUM_TICKS;
        case 1: return Q1_QUANTUM_TICKS;
        default: return Q2_QUANTUM_TICKS;
    }
}

static void boost_all_queues(void) {
    pcb_t* p;
    while ((p = dequeue_pcb(&Q1)) != NULL) {
        set_level_for_pid(p->pid, 0);
        enqueue_pcb(&Q0, p);
    }
    while ((p = dequeue_pcb(&Q2)) != NULL) {
        set_level_for_pid(p->pid, 0);
        enqueue_pcb(&Q0, p);
    }
}

static void drain_ready_into_Q0(queue_t* rq) {
    pcb_t* p;
    while ((p = dequeue_pcb(rq)) != NULL) {
        set_level_for_pid(p->pid, 0);
        enqueue_pcb(&Q0, p);
    }
}

static pcb_t* pick_next_from_mlfq(void) {
    pcb_t* p = dequeue_pcb(&Q0);
    if (p) return p;
    p = dequeue_pcb(&Q1);
    if (p) return p;
    return dequeue_pcb(&Q2);
}

static void enqueue_by_level(pcb_t* p, int level) {
    if (level <= 0) {
        enqueue_pcb(&Q0, p);
    } else if (level == 1) {
        enqueue_pcb(&Q1, p);
    } else {
        enqueue_pcb(&Q2, p);
    }
}

void mlfq_scheduler(uint32_t current_time_ms, queue_t *rq, queue_t *blocked_q /*unused*/, pcb_t **cpu_task) {
    // boost periódico
    if (last_boost_time_ms == 0) last_boost_time_ms = current_time_ms;
    if (current_time_ms - last_boost_time_ms >= BOOST_PERIOD_MS) {
        boost_all_queues();
        last_boost_time_ms = current_time_ms;
    }

    // novas tarefas para Q0
    drain_ready_into_Q0(rq);

    // avança 1 tick na tarefa do CPU (se existir)
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        current_slice_ticks += 1;

        // terminou?
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
            current_slice_ticks = 0;
        } else {
            // quantum esgotado → descer nível e preemptar
            int lvl = get_level_for_pid((*cpu_task)->pid);
            int qticks = quantum_for_level(lvl);
            if (current_slice_ticks >= qticks) {
                int new_lvl = (lvl < 2) ? (lvl + 1) : 2;
                set_level_for_pid((*cpu_task)->pid, new_lvl);
                enqueue_by_level(*cpu_task, new_lvl);
                *cpu_task = NULL;
                current_slice_ticks = 0;
            }
        }
    }

    // se o CPU está livre, escolher próxima (Q0 > Q1 > Q2)
    if (*cpu_task == NULL) {
        *cpu_task = pick_next_from_mlfq();
        if (*cpu_task) {
            if ((*cpu_task)->ellapsed_time_ms == 0) {
                printf("[METRIC] START pid=%d t_ms=%u\n", (*cpu_task)->pid, current_time_ms);
                fflush(stdout);
            }
            current_slice_ticks = 0;
        }
    }
}
