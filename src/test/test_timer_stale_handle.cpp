/**
 * test_timer_stale_handle.cpp
 *
 * Reproduces the crash described in the bug report:
 *
 *   SIGSEGV in TimerQueue::remove() at timer-queue.cpp:162
 *     entry->m_prev->m_next = entry->m_next ;
 *
 * Root cause: Timer G fires, doTimer() dequeues and later frees the
 * queueEntry_t, but the caller (simulating dlg->m_timerG) still holds the
 * stale raw pointer.  When clearSipTimers() is called (simulating an ACK
 * arriving), it calls remove() on the already-freed entry whose m_prev is
 * NULL → SIGSEGV.
 *
 * The test exercises that exact sequence:
 *   1. Add a timer (simulates setting timerG when 200 OK is sent)
 *   2. Let it fire  (doTimer dequeues + frees the entry, calls callback)
 *   3. In the callback, do NOT update stale_handle (simulates the window
 *      where retransmitFinalResponse hasn't called dlg->setTimerG yet)
 *   4. Call remove(stale_handle) from outside the callback (simulates ACK
 *      arriving and clearSipTimers running with the old handle)
 *
 * Expected result without the fix: SIGSEGV (m_prev == NULL dereference)
 * Expected result with the fix   : clean exit, assert passes
 *
 * Build (from repo root on the Linux build machine, after sofia-sip is built):
 *
 *   g++ -std=c++17 -DTEST -Isrc \
 *       -Ideps/sofia-sip/libsofia-sip-ua/su \
 *       -Ideps/sofia-sip/libsofia-sip-ua/nta \
 *       -Ideps/sofia-sip/libsofia-sip-ua/sip \
 *       -Ideps/sofia-sip/libsofia-sip-ua/msg \
 *       -Ideps/sofia-sip/libsofia-sip-ua/url \
 *       src/timer-queue.cpp \
 *       src/test/test_timer_stale_handle.cpp \
 *       deps/sofia-sip/libsofia-sip-ua/.libs/libsofia-sip-ua.a \
 *       -lpthread -o test_timer_stale_handle
 *
 *   ./test_timer_stale_handle
 *
 * Without the fix the process will SIGSEGV.
 * With the fix it should print "PASSED" and exit 0.
 */

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <cassert>

#include "sofia-sip/su.h"
#include "sofia-sip/su_wait.h"

#define TEST  // suppress DR_LOG calls inside timer-queue.cpp
#include "timer-queue.hpp"

using namespace drachtio;

/* -------------------------------------------------------------------------
 * Globals shared between callbacks (mirrors dlg->m_timerG state)
 * ---------------------------------------------------------------------- */
static su_root_t*        g_root        = nullptr;
static TimerQueue*       g_queue       = nullptr;
static TimerEventHandle  g_stale_handle = nullptr; // simulates dlg->m_timerG

/* Set to true once the timer callback has fired */
static bool              g_timer_fired = false;

/* -------------------------------------------------------------------------
 * This simulates what retransmitFinalResponse does when timerG fires:
 *   - retransmit the 200 OK  (just a print here)
 *   - set a NEW timerG  (we deliberately skip this to simulate the race)
 *
 * In the race window the old handle is stale (freed by doTimer) but
 * g_stale_handle still points to it.
 * ---------------------------------------------------------------------- */
static void simulate_retransmit_final_response(void* /*arg*/)
{
    std::cout << "[callback] timerG fired - simulating retransmitFinalResponse\n";
    std::cout << "[callback] NOT updating stale_handle (simulating race window)\n";
    g_timer_fired = true;
    /* In production code this is where dlg->setTimerG(new_handle) would be
     * called.  We intentionally omit it to leave g_stale_handle dangling,
     * which is exactly the state captured in the core dump. */
}

/* -------------------------------------------------------------------------
 * Called by sofia after a short delay, once the event loop is running.
 * This is the top-level test driver.
 * ---------------------------------------------------------------------- */
static void run_test(su_root_magic_t* /*magic*/, su_timer_t* t, su_timer_arg_t* /*arg*/)
{
    su_timer_destroy(t);

    std::cout << "--- test_timer_stale_handle ---\n";
    std::cout << "Step 1: add timerG (50ms) - simulates sending 200 OK\n";

    g_stale_handle = g_queue->add(simulate_retransmit_final_response, nullptr, 50);
    assert(g_stale_handle != nullptr);
    assert(g_queue->size() == 1);

    std::cout << "Step 1 OK: handle=" << (void*)g_stale_handle
              << "  queue size=" << g_queue->size() << "\n";

    /* The timer will fire after 50ms inside the event loop.
     * We schedule the "ACK arrives" check 150ms from now (well after the
     * timer fires) to simulate the ACK coming in after timerG has fired
     * and freed its entry but before dlg->m_timerG was updated. */
    su_timer_t* ack_timer = su_timer_create(su_root_task(g_root), 200);
    su_timer_set_interval(ack_timer, [](su_root_magic_t*, su_timer_t* tt, su_timer_arg_t*) {
        su_timer_destroy(tt);

        std::cout << "Step 2: ACK arrives - calling remove(stale_handle)\n";
        std::cout << "        stale_handle=" << (void*)g_stale_handle
                  << "  queue size=" << g_queue->size()
                  << "  timer_fired=" << g_timer_fired << "\n";

        assert(g_timer_fired && "timer should have fired before ACK arrives");

        /* This is the crash: remove() is called with the handle that doTimer()
         * already dequeued and freed.  entry->m_prev == NULL, so the else
         * branch in remove() dereferences a null pointer → SIGSEGV. */
        if (g_queue->size() == 0) {
            /* Queue is empty: entry was dequeued by doTimer, handle is stale.
             * Calling remove() here triggers the crash. */
            std::cout << "        queue is empty but we still hold stale handle - "
                         "calling remove() to trigger crash...\n";
            g_queue->remove(g_stale_handle);  // <-- SIGSEGV without the fix
        }

        std::cout << "PASSED - remove(stale_handle) did not crash\n";
        su_root_break(g_root);
    }, nullptr, 150);
}

int main()
{
    su_init();
    g_root  = su_root_create(nullptr);
    g_queue = new TimerQueue(g_root, "timerG");

    /* Kick off the test after the event loop starts */
    su_timer_t* start = su_timer_create(su_root_task(g_root), 100);
    su_timer_set_interval(start, run_test, nullptr, 10);

    su_root_run(g_root);

    delete g_queue;
    su_root_destroy(g_root);
    su_deinit();
    return 0;
}
