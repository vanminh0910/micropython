
#pragma once

#include <limits.h>
#include <stdio.h>

#include "nrfx.h"
#include "hal/nrf_rtc.h"

#include "nimble/nimble_port.h"

#include "nimble.h"

#define BLE_NPL_TIME_FOREVER    UINT_MAX
#define BLE_NPL_OS_ALIGNMENT    4
#define OS_TICKS_PER_SEC        RTC_INPUT_FREQ

typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;

struct ble_npl_mutex {
    bool locked;
};

struct ble_npl_sem {
    uint16_t tokens;
};

struct ble_npl_event {
    ble_npl_event_fn *cb;
    void *arg;
    struct ble_npl_event *prev;
    struct ble_npl_event *next;
};

struct ble_npl_eventq {
    struct ble_npl_event *head;
    struct ble_npl_event *tail;
};

struct ble_npl_callout {
    struct ble_npl_event ev;
    struct ble_npl_eventq *evq;
    struct ble_npl_callout *prev;
    struct ble_npl_callout *next;
    uint32_t ticks;
};


static inline uint32_t ble_npl_hw_enter_critical(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void ble_npl_hw_exit_critical(uint32_t primask) {
    __set_PRIMASK(primask);
}


static inline uint32_t ble_npl_time_get(void) {
    uint32_t counter = NRF_RTC1->COUNTER;
    bleprintf("nimble: ble_npl_time_get: %ld\n", counter);
    return counter;
}

static inline ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms) {
    return ((uint64_t)ms * OS_TICKS_PER_SEC) / OS_TICKS_PER_SEC;
}

static inline ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out_ticks) {
    // TODO: check overflow
    *out_ticks = ble_npl_time_ms_to_ticks32(ms);
    return BLE_NPL_OK;
}


static inline bool ble_npl_os_started(void) {
    return nimble_started;
}

static inline void * ble_npl_get_current_task_id(void) {
    // Ihis task ID is only used as ID, so we can put anything in there.
    return nimble_current_task;
}

static inline void ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn *cb, void *arg) {
    bleprintf("nimble: init event %p, cb=%p, arg=%p\n", ev, cb, arg);
    ev->cb = cb;
    ev->arg = arg;
    ev->prev = NULL;
    ev->next = NULL;
}

static inline void * ble_npl_event_get_arg(struct ble_npl_event *ev) {
    return ev->arg;
}

static inline void ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg) {
    ev->arg = arg;
}

static inline bool ble_npl_event_is_queued(struct ble_npl_event *ev) {
    return ev->next != NULL || ev->prev != NULL;
}

static inline void ble_npl_event_run(struct ble_npl_event *ev) {
    ev->cb(ev);
}

static inline void ble_npl_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev) {
    uint32_t sr = ble_npl_hw_enter_critical();
    bleprintf("nimble: ble_npl_eventq_remove: event %p from queue %p\n", ev, evq);
    if (ev == evq->head) {
        evq->head = evq->head->next;
    }
    if (ev == evq->tail) {
        evq->tail = evq->tail->prev;
    }
    if (ev->next != NULL) {
        ev->next->prev = ev->prev;
    }
    if (ev->prev != NULL) {
        ev->prev->next = ev->next;
    }
    ev->next = NULL;
    ev->prev = NULL;
    ble_npl_hw_exit_critical(sr);
}

static inline void ble_npl_eventq_init(struct ble_npl_eventq *evq) {
    evq->head = NULL;
    evq->tail = NULL;
}

static inline void ble_npl_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq, ble_npl_event_fn *cb, void *arg) {
    co->ev.cb = cb;
    co->ev.arg = arg;
    co->evq = evq;
    co->prev = NULL;
    co->next = NULL;
    co->ticks = BLE_NPL_TIME_FOREVER;
}

static inline bool ble_npl_callout_is_active(struct ble_npl_callout *co) {
    bleprintf("nimble: ble_npl_callout_is_active %p\n", co);
    return co->ticks != BLE_NPL_TIME_FOREVER;
}

static inline ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *co) {
    bleprintf("nimble: ble_npl_callout_get_ticks\n");
    return co->ticks;
}

static inline ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *mu) {
    mu->locked = false;
    return BLE_NPL_OK;
}

static inline ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *mu, ble_npl_time_t timeout) {
    if (timeout != BLE_NPL_TIME_FOREVER) {
        printf("TODO: ble_npl_mutex_pend with timeout\n");
        while (1) {}
    }
    uint32_t sr = ble_npl_hw_enter_critical();
    while (mu->locked) {
        ble_npl_hw_exit_critical(sr);
        __WFI();
        sr = ble_npl_hw_enter_critical();
    }
    mu->locked = true;
    ble_npl_hw_exit_critical(sr);

    return BLE_NPL_OK;
}

static inline ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *mu) {
    mu->locked = false; // atomic write
    return BLE_NPL_OK;
}

static inline ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens) {
    sem->tokens = tokens;
    return BLE_NPL_OK;
}

static inline uint16_t ble_npl_sem_get_count(struct ble_npl_sem *sem) {
    return sem->tokens;
}

void ble_npl_hw_set_isr(int irqn, void (*addr)());
