/* QEMU virt -bios none enters this sole-hart, flat, M-mode image at 0x80000000.
 * Reuse the integer bootstrap; the local linker supplies this board's RAM map.
 * a0/a1 boot hart/DTB arguments are not consumed: this pinned profile has one
 * hart and a fixed documented device map, not general device-tree discovery.
 */
#include "../riscue/start.S"
