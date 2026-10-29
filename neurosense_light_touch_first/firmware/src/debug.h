// Debug infrastructure. Compiled in only with -DNS_DEBUG=1 (env:debug).
//
// In release builds every DBG_* macro expands to ((void)0): no logger task, no buffers,
// no Serial, and macro arguments are never evaluated. Code that cannot live inside a
// macro is wrapped in `#if NS_DEBUG` or marked with the tag below, so every piece can be
// found with Ctrl+F for "NS_DEBUG" (flag) or "DBG_" (macros) and removed.
#pragma once
#include <Arduino.h>

#ifndef NS_DEBUG
#define NS_DEBUG 0
#endif

#if NS_DEBUG

struct DbgCounters {
  uint32_t isrDrops = 0;               // ISR could not queue an event (ISR is the only writer)
  uint32_t queueDrops = 0;             // task-side post failed
  uint32_t queueDepthMax = 0;
  uint32_t maxQueueLatencyMs = 0;      // posted -> handled (reveals blocking)
  uint32_t maxEventUs = 0;             // longest single event handler
  uint32_t presses = 0;
  uint32_t debounceRetries = 0;
  uint32_t notifyOk = 0, notifyFail = 0;
  uint32_t reconnects = 0, advRestarts = 0;
  uint32_t pendingDropped = 0, pendingExpired = 0;
  uint32_t logDropped = 0;
};
extern DbgCounters g_dbg;

void dbg_init();                                     // call first thing in setup()
void dbg_log(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void dbg_assert_fail(const char* expr, const char* file, int line);
void dbg_periodic(uint32_t now);                     // from the app task; reports on a timer
void dbg_report_now(const char* why);                // memory + stacks + counters
void dbg_set_queue(QueueHandle_t q);

#define DBG_INIT()            dbg_init()
#define DBG_LOG(tag, ...)     dbg_log(tag, __VA_ARGS__)
#define DBG_ASSERT(c)         do { if (!(c)) dbg_assert_fail(#c, __FILE__, __LINE__); } while (0)
#define DBG_INC(field)        (g_dbg.field++)
#define DBG_MAX(field, val)   do { uint32_t v_ = (val); if (v_ > g_dbg.field) g_dbg.field = v_; } while (0)
#define DBG_PERIODIC(now)     dbg_periodic(now)
#define DBG_REPORT(why)       dbg_report_now(why)
#define DBG_SET_QUEUE(q)      dbg_set_queue(q)
#define DBG_STMT(x)           x

#else  // release: nothing is compiled, nothing is evaluated

#define DBG_INIT()            ((void)0)
#define DBG_LOG(tag, ...)     ((void)0)
#define DBG_ASSERT(c)         ((void)0)
#define DBG_INC(field)        ((void)0)
#define DBG_MAX(field, val)   ((void)0)
#define DBG_PERIODIC(now)     ((void)0)
#define DBG_REPORT(why)       ((void)0)
#define DBG_SET_QUEUE(q)      ((void)0)
#define DBG_STMT(x)

#endif
