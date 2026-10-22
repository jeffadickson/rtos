#pragma once
/** @file
 * @brief Optional word-RMW HAL, independent of the kernel processor HAL.
 * Providers operate only on naturally aligned, atomic-capable normal RAM.
 * They supply exchange/add/or/and with acquire-release ordering and return the
 * old word. They do not change interrupt masks, wait on software locks or provide
 * compare/swap. The CPU may still stall on the platform memory transaction.
 * The provider owns instruction encodings and compiler/hardware ordering;
 * the BSP owns memory-region capability, device fences and cache contracts.
 */
#include "rtos_config.hpp"
#if !RTOS_ZAAMO
#error "The hardware atomic HAL requires RTOS_ZAAMO"
#endif
#include "rtos_hal_atomic_port.hpp"
