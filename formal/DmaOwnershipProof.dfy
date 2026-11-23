// SPDX-License-Identifier: BSD-2-Clause
// Formal Dafny proof of NexRT's DMA ownership/quarantine state machine (src/dma.cpp)
// and overflow-free half-open storage overlap detection (src/scheduler.hpp).

module NexRTDmaOwnershipProof {

  datatype DmaState = Uninitialized | Idle | Active | Completed | Failed | Quarantined

  const U32_MAX: int := 4294967295

  // Ghost model of rtos::dma::Channel paired with physical hardware bus ownership.
  class DmaChannelModel {
    var state: DmaState
    var generation: int
    var hwOwnsBuffer: bool // Physical truth: DMA engine may still be accessing staging RAM.

    // Core high-reliability safety invariant:
    //   1. Staging is NEVER Idle while hardware might still access the buffer.
    //   2. Generation never wraps or exceeds U32_MAX.
    //   3. Active or Quarantined states always carry a non-zero generation ticket.
    predicate Valid()
      reads this
    {
      0 <= generation <= U32_MAX &&
      (state == Uninitialized ==> !hwOwnsBuffer && generation == 0) &&
      (state == Idle ==> !hwOwnsBuffer) &&
      (state == Completed ==> !hwOwnsBuffer && generation > 0) &&
      (state == Failed ==> !hwOwnsBuffer && generation > 0) &&
      (state == Active ==> generation > 0) &&
      (state == Quarantined ==> generation > 0)
    }

    constructor ()
      ensures Valid()
      ensures state == Uninitialized && !hwOwnsBuffer && generation == 0
    {
      state := Uninitialized;
      generation := 0;
      hwOwnsBuffer := false;
    }

    method Initialize()
      requires Valid() && state == Uninitialized
      modifies this
      ensures Valid() && state == Idle && !hwOwnsBuffer
    {
      state := Idle;
    }

    // Launch a transfer: refuses generation wrap (Exhausted) and returns strictly fresh ticket.
    method StartTransfer(hwAcceptsStart: bool) returns (ok: bool, ticket: int)
      requires Valid() && state == Idle
      modifies this
      ensures Valid()
      ensures ok ==> state == Active && ticket == generation && ticket > old(generation) && hwOwnsBuffer
      ensures !ok ==> state == Idle && !hwOwnsBuffer
    {
      if generation == U32_MAX {
        return false, 0; // Status::exhausted — NEVER wraps generation!
      }
      generation := generation + 1;
      if !hwAcceptsStart {
        // Backend start()==false guarantees no DMA transfer was issued.
        return false, 0;
      }
      hwOwnsBuffer := true;
      state := Active;
      return true, generation;
    }

    // Cancel an active transfer: if hardware stop() fails to prove bus quiescence,
    // staging MUST be Quarantined and NEVER released back to Idle!
    method Cancel(ticket: int, hwStopProvesQuiescence: bool)
      requires Valid()
      requires state == Active && ticket == generation
      modifies this
      ensures Valid()
      ensures !hwStopProvesQuiescence ==> state == Quarantined
      ensures hwStopProvesQuiescence ==> state == Idle && !hwOwnsBuffer
    {
      if !hwStopProvesQuiescence {
        // Hardware may still be writing staging RAM -> quarantine fail-closed!
        state := Quarantined;
        return;
      }
      hwOwnsBuffer := false;
      state := Idle;
    }

    // Recover from Quarantined only once hardware proves quiescence.
    method Recover(ticket: int, hwStopProvesQuiescence: bool)
      requires Valid()
      requires state == Quarantined && ticket == generation
      modifies this
      ensures Valid()
      ensures !hwStopProvesQuiescence ==> state == Quarantined
      ensures hwStopProvesQuiescence ==> state == Idle && !hwOwnsBuffer
    {
      if !hwStopProvesQuiescence {
        return;
      }
      hwOwnsBuffer := false;
      state := Idle;
    }

    // Stale completion check: proves an older ticket can NEVER match a newer active transfer.
    lemma LemmaStaleTicketRejected(staleTicket: int)
      requires Valid()
      requires state == Active
      requires 0 <= staleTicket < generation
      ensures staleTicket != generation
    {}
  }

  // Exact proof of rtos::detail::ranges_overlap(a, a_bytes, b, b_bytes) from src/scheduler.hpp:
  // Overflow-free half-open range intersection without ever computing a + a_bytes or b + b_bytes.
  predicate RangesOverlap(a: int, aBytes: int, b: int, bBytes: int)
    requires a >= 0 && aBytes >= 0 && b >= 0 && bBytes >= 0
  {
    aBytes > 0 && bBytes > 0 &&
    (if a <= b then b - a < aBytes else a - b < bBytes)
  }

  predicate InHalfOpen(addr: int, start: int, len: int) {
    start <= addr < start + len
  }

  lemma LemmaRangesOverlapExactAndSymmetric(a: int, aBytes: int, b: int, bBytes: int)
    requires a >= 0 && aBytes >= 0 && b >= 0 && bBytes >= 0
    ensures RangesOverlap(a, aBytes, b, bBytes) <==> RangesOverlap(b, bBytes, a, aBytes)
    ensures RangesOverlap(a, aBytes, b, bBytes) ==>
            InHalfOpen(if a <= b then b else a, a, aBytes) &&
            InHalfOpen(if a <= b then b else a, b, bBytes)
    ensures forall addr :: InHalfOpen(addr, a, aBytes) && InHalfOpen(addr, b, bBytes) ==>
            RangesOverlap(a, aBytes, b, bBytes)
  {}
}
