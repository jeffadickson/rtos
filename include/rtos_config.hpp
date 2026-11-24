#pragma once
#ifndef RTOS_DMA
#define RTOS_DMA 0
#endif
static_assert(RTOS_DMA==0 || RTOS_DMA==1);
#include <stdint.h>
#ifndef RTOS_MAX_THREADS
#define RTOS_MAX_THREADS 16
#endif
#ifndef RTOS_TICK_HZ
#define RTOS_TICK_HZ 1000
#endif
#ifndef RTOS_IDLE_STACK_BYTES
#define RTOS_IDLE_STACK_BYTES 1024
#endif
#ifndef RTOS_ROUND_ROBIN
#define RTOS_ROUND_ROBIN 1
#endif
#ifndef RTOS_METRICS
#define RTOS_METRICS 1
#endif
#ifndef RTOS_STACK_GUARD
#define RTOS_STACK_GUARD 1
#endif
#ifndef RTOS_STACK_WATERMARK
#define RTOS_STACK_WATERMARK 0
#endif
#ifndef RTOS_MTIME_ADDRESS
#define RTOS_MTIME_ADDRESS 0x0200bff8u
#endif
#ifndef RTOS_MTIMECMP_ADDRESS
#define RTOS_MTIMECMP_ADDRESS 0x02004000u
#endif
#ifndef RTOS_IPC_MAX_MESSAGE_BYTES
#define RTOS_IPC_MAX_MESSAGE_BYTES 128
#endif
static_assert(RTOS_IPC_MAX_MESSAGE_BYTES>0 && RTOS_IPC_MAX_MESSAGE_BYTES<=512);
#ifndef RTOS_MAX_HELD_MUTEXES
#define RTOS_MAX_HELD_MUTEXES 8
#endif
static_assert(RTOS_MAX_HELD_MUTEXES>0 && RTOS_MAX_HELD_MUTEXES<=255);
#ifndef RTOS_LOGGING
#define RTOS_LOGGING 0
#endif
#ifndef RTOS_SYNC
#define RTOS_SYNC 0
#endif
#ifndef RTOS_HW_PROTECT
#define RTOS_HW_PROTECT 0
#endif
#ifndef RTOS_BUDGET_WATCHDOG
#define RTOS_BUDGET_WATCHDOG 0
#endif
#ifndef RTOS_HW_SCHED
#define RTOS_HW_SCHED 0
#endif
#ifndef RTOS_HWSCHED_BASE
#define RTOS_HWSCHED_BASE 0x10010000u
#endif
#ifndef RTOS_RISCUE_BANKED
#define RTOS_RISCUE_BANKED 0
#endif
#ifndef RTOS_RISCUE_BANKS
#define RTOS_RISCUE_BANKS 8
#endif
static_assert(RTOS_SYNC == 0 || RTOS_SYNC == 1);
static_assert(RTOS_LOGGING == 0 || RTOS_LOGGING == 1);
static_assert(RTOS_MAX_THREADS >= 1 && RTOS_MAX_THREADS <= 255);
static_assert(RTOS_TICK_HZ > 0 && RTOS_TICK_HZ <= UINT32_MAX);
static_assert(RTOS_IDLE_STACK_BYTES >= 512 && RTOS_IDLE_STACK_BYTES % 16 == 0);
static_assert(RTOS_ROUND_ROBIN == 0 || RTOS_ROUND_ROBIN == 1);
static_assert(RTOS_METRICS == 0 || RTOS_METRICS == 1);
static_assert(RTOS_STACK_GUARD == 0 || RTOS_STACK_GUARD == 1);
static_assert(RTOS_STACK_WATERMARK == 0 || RTOS_STACK_WATERMARK == 1);
static_assert(RTOS_HW_PROTECT == 0 || RTOS_HW_PROTECT == 1);
static_assert(RTOS_BUDGET_WATCHDOG == 0 || RTOS_BUDGET_WATCHDOG == 1);
static_assert(RTOS_HW_SCHED == 0 || RTOS_HW_SCHED == 1);
static_assert(RTOS_RISCUE_BANKED == 0 || RTOS_RISCUE_BANKED == 1);
static_assert(RTOS_RISCUE_BANKS >= 1 && RTOS_RISCUE_BANKS <= 16);
static_assert(RTOS_MTIME_ADDRESS % 4 == 0 && RTOS_MTIMECMP_ADDRESS % 4 == 0);
static_assert(RTOS_HWSCHED_BASE % 4 == 0);
static_assert(!RTOS_HW_SCHED || RTOS_MAX_THREADS <= 16);
static_assert(!RTOS_RISCUE_BANKED || RTOS_HW_SCHED);

#ifndef RTOS_STORAGE
#define RTOS_STORAGE 0
#endif
static_assert(RTOS_STORAGE == 0 || RTOS_STORAGE == 1);
static_assert(!RTOS_LOGGING || RTOS_SYNC);
