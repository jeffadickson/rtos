/* Maximum four concurrent variable-sized packet allocations, all fixed-size
 * 1600-byte blocks. Exhaustion returns NULL; no fallback pool or heap exists.
 */
LWIP_MALLOC_MEMPOOL_START
LWIP_MALLOC_MEMPOOL(4, 1600)
LWIP_MALLOC_MEMPOOL_END
