/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Ayke van Laethem
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "nimble.h"
#include "blenus.h"
#include "host/ble_hs.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_hci.h"
#include "nimble/nimble_port.h"

#include "hal/nrf_clock.h"
#include "hal/nrf_rtc.h"

static void nimble_schedule();
static void ble_npl_callout_remove(struct ble_npl_callout *co);

uint8_t nimble_host_task;
uint8_t nimble_ll_task;
uint8_t *nimble_current_task = &nimble_host_task;
uint8_t nimble_started = false;
static struct ble_npl_callout *nimble_callout_head;
static struct ble_npl_callout *nimble_callout_tail;

static void ble_set_addr() {
    ble_addr_t addr;
    ble_hs_id_gen_rnd(1, &addr);
    ble_hs_id_set_rnd(addr.val);
}

static void check_rc(int rc, char *msg) {
    if (rc != 0) {
        printf("error: returned %d from %s\n", rc, msg);
    }
}

static void ble_on_sync() {
    ble_set_addr();
    nus_advertise();
}

void nimble_init() {
    bleprintf("\n\nnimble: init\n");

    // enable clock
    nrf_clock_task_trigger(NRF_CLOCK_TASK_LFCLKSTART);

    // enable RTC for the scheduler
    NRF_RTC1->EVTENSET = RTC_EVTEN_COMPARE0_Msk;
    nrf_rtc_task_trigger(NRF_RTC1, NRF_RTC_TASK_START);
    NVIC_SetPriority(RTC1_IRQn, 1);
    NVIC_EnableIRQ(RTC1_IRQn);

    // enable SWI0: LL task
    NVIC_SetPriority(SWI0_IRQn, 4); // TODO
    NVIC_EnableIRQ(SWI0_IRQn);

    // enable SWI1: host task
    NVIC_SetPriority(SWI1_IRQn, 5); // TODO
    NVIC_EnableIRQ(SWI1_IRQn);

    int rc;
    nimble_port_init();

    // Init LL task.
    rc = ble_phy_init();
    check_rc(rc, "phy init");
    rc = ble_phy_txpwr_set(MYNEWT_VAL(BLE_LL_TX_PWR_DBM));
    check_rc(rc, "phy txpwr");
    rc = ble_ll_hci_send_noop();
    check_rc(rc, "ll hci send");
    rc = ble_ll_rand_start();
    check_rc(rc, "ll rand start");

    ble_hs_cfg.sync_cb = ble_on_sync;
    nus_init();

    uint32_t sr = ble_npl_hw_enter_critical();
    nimble_started = true;
    nimble_schedule();
    ble_npl_hw_exit_critical(sr);
}

// Process LL event
void SWI0_IRQHandler() {
    bleprintf("++ prio: LL\n");
    nimble_current_task = &nimble_ll_task;
    struct ble_npl_event *ev = ble_npl_eventq_get(&g_ble_ll_data.ll_evq, 0);
    if (ev == NULL) {
        bleprintf("  no event?\n");
        return;
    }
    bleprintf("  running %p (fp %p)\n", ev, ev->cb);
    ble_npl_event_run(ev);
    bleprintf("  done    %p\n", ev);
    nimble_current_task = &nimble_host_task;
    bleprintf("-- prio: LL\n");
}

// Process host event
void SWI1_IRQHandler() {
    printf("++ prio: host\n");
    struct ble_npl_event *ev = ble_npl_eventq_get(nimble_port_get_dflt_eventq(), 0);
    if (ev == NULL) {
        bleprintf("  no event?\n");
        return;
    }
    printf("  running %p (fp %p)\n", ev, ev->cb);
    ble_npl_event_run(ev);
    bleprintf("  done    %p\n", ev);
    printf("-- prio: host\n");
}

// WARNING: this function needs to be called with interrupts disabled!
static void nimble_schedule() {
    // Pick highest priority event to schedule.
    if (g_ble_ll_data.ll_evq.head != NULL) {
        NVIC_SetPendingIRQ(SWI0_IRQn);
    } else if (nimble_port_get_dflt_eventq()->head != NULL) {
        NVIC_SetPendingIRQ(SWI1_IRQn);
    } else {
        printf("TODO: nothing to schedule\n");
        return;
    }
}

// Scheduler for callouts. Runs at a high priority and queues host and LL
// events.
void RTC1_IRQHandler() {
    // clear IRQ event
    NRF_RTC1->EVENTS_COMPARE[0] = 0;

    uint32_t sr = ble_npl_hw_enter_critical();
    struct ble_npl_callout *co = nimble_callout_head;
    if (co == NULL) {
        NRF_RTC1->INTENCLR = RTC_INTENCLR_COMPARE0_Msk;
        ble_npl_hw_exit_critical(sr);
        return; // nothing to schedule
    }

    // trigger event
    uint32_t now = NRF_RTC1->COUNTER;
    if (co->ticks <= now + 1) {
        if (co->ticks == now + 1) {
            do { // N+1 problem
                // busy-wait until ready (uncommon)
                now = NRF_RTC1->COUNTER;
            } while (co->ticks != now + 1);
        }
        ble_npl_callout_remove(co);
        ble_npl_eventq_put(co->evq, &co->ev);
        if (nimble_callout_head != NULL) {
            // tail call for next event
            NVIC_SetPendingIRQ(RTC1_IRQn);
        } else {
            NRF_RTC1->INTENCLR = RTC_INTENCLR_COMPARE0_Msk;
        }
    } else {
        NRF_RTC1->CC[0] = co->ticks;
        NRF_RTC1->INTENSET = RTC_INTENSET_COMPARE0_Msk;
    }
    ble_npl_hw_exit_critical(sr);
}

// Pull a single event from the front of the event queue.
struct ble_npl_event * ble_npl_eventq_get(struct ble_npl_eventq *evq, ble_npl_time_t timeout) {
    if (timeout == 0) {
        uint32_t sr = ble_npl_hw_enter_critical();
        if (evq->head == NULL) {
            ble_npl_hw_exit_critical(sr);
            return NULL;
        }
        struct ble_npl_event *ev = evq->head;
        ble_npl_eventq_remove(evq, ev);
        ble_npl_hw_exit_critical(sr);
        bleprintf("nimble: get event %p from queue %p\n", ev, evq);
        return ev;
    } else {
        printf("TODO: ble_npl_eventq_get timeout\n");
        while (1) {}
    }
    return NULL;
}

// Add an event to the back of the event queue.
void ble_npl_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev) {
    bleprintf("nimble: put event %p in queue %p\n", ev, evq);
    uint32_t sr = ble_npl_hw_enter_critical();
    if (evq->tail == NULL) {
        // first event in queue
        evq->tail = ev;
        evq->head = ev;
    } else {
        // num events >= 1
        ev->prev = evq->tail;
        ev->prev->next = ev;
        evq->tail = ev;
    }
    if (nimble_started) {
        nimble_schedule();
    }
    ble_npl_hw_exit_critical(sr);
}

// Remove this callout from the callout list.
// Must be called in a critical section.
static void ble_npl_callout_remove(struct ble_npl_callout *co) {
    co->ticks = BLE_NPL_TIME_FOREVER;
    // remove from queue
    if (co->prev == NULL) { // co == nimble_callout_head
        nimble_callout_head = co->next;
        nimble_callout_head->prev = NULL;
    } else {
        co->prev->next = co->next;
        co->prev = NULL;
    }
    if (co->next == NULL) { // co == nimble_callout_tail
        nimble_callout_tail = co->prev;
        nimble_callout_tail->next = NULL;
    } else {
        co->next->prev = co->prev;
        co->next = NULL;
    }
}

// Remove this callout from the callout list.
void ble_npl_callout_stop(struct ble_npl_callout *co) {
    uint32_t sr = ble_npl_hw_enter_critical();
    if (co->ticks == BLE_NPL_TIME_FOREVER) {
        return; // not queued
    }
    ble_npl_callout_remove(co);
    ble_npl_hw_exit_critical(sr);
}

// Set (or reset) the timeout for this callout timer.
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *co, ble_npl_time_t ticks) {
    uint32_t sr = ble_npl_hw_enter_critical();
    printf("nimble: ble_npl_callout_reset: %p (%lu ticks)\n", co, ticks);
    if (co->ticks != BLE_NPL_TIME_FOREVER) {
        bleprintf("  remove first\n");
        ble_npl_callout_remove(co);
    }
    co->ticks = ble_npl_time_get() + ticks;
    // insert in callout list
    if (nimble_callout_head == NULL) {
        printf("  insert as head+tail\n");
        // insert as only element
        nimble_callout_head = co;
        nimble_callout_tail = co;
    } else if (nimble_callout_head->ticks > co->ticks) {
        // insert at the front
        nimble_callout_head->prev = co;
        co->next = nimble_callout_head;
        nimble_callout_head = co;
    } else if (nimble_callout_tail->ticks <= co->ticks) {
        // insert at the back
        nimble_callout_tail->next = co;
        co->prev = nimble_callout_tail;
        nimble_callout_tail = co;
    } else {
        // insert somewhere in the middle
        printf("  TODO: insert in the middle\n");
        while (1) {}
        //struct ble_npl_callout *before = nimble_callout_head;
        //while (before->ticks <= co->ticks) {
        //    before = before->next;
        //}
        //if (before->next == NULL) {
        //} else {
        //    co->prev = before;
        //    co->next = before->next;
        //    before->next->prev = co;
        //    before->next = co;
        //}
    }
    ble_npl_hw_exit_critical(sr);
    NVIC_SetPendingIRQ(RTC1_IRQn);
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *sem, ble_npl_time_t timeout) {
    bleprintf("nimble: ble_npl_sem_pend: %p (%lu ticks)\n", sem, timeout);
    uint32_t cf = ble_npl_hw_enter_critical();
    if (timeout == BLE_NPL_TIME_FOREVER) {
        // Wait until the semaphore has been released (assuming it needs
        // an interrupt).
        while (sem->tokens == 0) {
            ble_npl_hw_exit_critical(cf);
            nimble_schedule();
            __WFI();
            cf = ble_npl_hw_enter_critical();
        }
    } else {
        if (sem->tokens == 0) {
            ble_npl_hw_exit_critical(cf);
            printf("  TODO: wait\n");
            while (1) {}
        }
    }
    sem->tokens -= 1;
    ble_npl_hw_exit_critical(cf);
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *sem) {
    bleprintf("nimble: ble_npl_sem_release: %p\n", sem);
    uint32_t cf = ble_npl_hw_enter_critical();
    sem->tokens += 1;
    ble_npl_hw_exit_critical(cf);

    return BLE_NPL_OK;
}

static void (*radio_isr_addr)();
static void (*rng_isr_addr)();
static void (*rtc0_isr_addr)();

void RADIO_IRQHandler() {
    radio_isr_addr();
}

void RNG_IRQHandler(void) {
    rng_isr_addr();
}

void RTC0_IRQHandler(void) {
    rtc0_isr_addr();
}

void ble_npl_hw_set_isr(int irqn, void (*addr)()) {
    switch (irqn) {
    case RADIO_IRQn:
        radio_isr_addr = addr;
        break;
    case RNG_IRQn:
        rng_isr_addr = addr;
        break;
    case RTC0_IRQn:
        rtc0_isr_addr = addr;
        break;
    }
}
