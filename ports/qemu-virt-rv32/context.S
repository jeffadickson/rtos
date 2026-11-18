/* RV32 integer ABI/trap frame is identical to RISCue. No FP/vector or S-mode
 * context is enabled. Reuse one implementation rather than fork its assembly.
 */
#include "../riscue/context.S"
