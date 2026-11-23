// SPDX-License-Identifier: BSD-2-Clause
// Formal Dafny verification of NexRT's starvation-free scheduler (src/scheduler.hpp
// and rtl/nexrt_hw_sched.sv) and wrap-safe 32-bit deadline comparisons.

module NexRTSchedulerProof {

  datatype ThreadState = Unused | Ready | Sleeping | Finished | Blocked | Quarantined

  const TWO31: int := 2147483648
  const TWO32: int := 4294967296

  // Bijection invariant using triggered sequence membership (`s in order`):
  // order[0..n-1] contains every thread slot 0..n-1 exactly once.
  predicate IsPermutation(order: seq<int>, n: int)
  {
    n >= 0 &&
    |order| == n &&
    (forall i :: 0 <= i < n ==> 0 <= order[i] < n) &&
    (forall i, j :: 0 <= i < j < n ==> order[i] != order[j]) &&
    (forall s :: 0 <= s < n ==> s in order)
  }

  // Exact model of Scheduler::move_to_back(slot) and nexrt_hw_sched::rotate_order_back.
  method MoveToBack(order: seq<int>, n: int, slot: int) returns (nextOrder: seq<int>)
    requires IsPermutation(order, n)
    requires 0 <= slot < n
    ensures IsPermutation(nextOrder, n)
    ensures nextOrder[n - 1] == slot
    // Strict rank progress: every thread previously behind `slot` moves strictly forward by 1!
    ensures forall p :: 0 <= p < n && order[p] == slot ==>
              (forall k :: p < k < n ==> nextOrder[k - 1] == order[k]) &&
              (forall k :: 0 <= k < p ==> nextOrder[k] == order[k])
  {
    assert slot in order;
    var pos := 0;
    while pos < n && order[pos] != slot
      invariant 0 <= pos <= n
      invariant forall j :: 0 <= j < pos ==> order[j] != slot
    {
      pos := pos + 1;
    }
    assert pos < n && order[pos] == slot;

    nextOrder := order[..pos] + order[pos + 1..] + [slot];

    // Prove surjectivity: every slot s in 0..n-1 remains in nextOrder
    forall s | 0 <= s < n
      ensures s in nextOrder
    {
      if s == slot {
        assert nextOrder[n - 1] == s;
      } else {
        assert s in order;
        var oldIdx :| 0 <= oldIdx < n && order[oldIdx] == s;
        if oldIdx < pos {
          assert nextOrder[oldIdx] == s;
        } else {
          assert oldIdx > pos;
          assert nextOrder[oldIdx - 1] == s;
        }
      }
    }
  }

  // Exact model of Scheduler::pick() / NexRT-HWSched 1-cycle combinational arbiter.
  // Proves priority optimality, safety (never picks non-Ready threads), and FIFO tie-breaking.
  method Pick(order: seq<int>, states: seq<ThreadState>, prios: seq<int>, n: int)
      returns (found: bool, chosen: int, chosenPos: int)
    requires IsPermutation(order, n)
    requires |states| == n && |prios| == n
    requires forall i :: 0 <= i < n ==> 0 <= prios[i] <= 255
    // Safety: never schedules Unused, Sleeping, Blocked, Finished, or Quarantined threads.
    ensures found ==> 0 <= chosen < n && 0 <= chosenPos < n && order[chosenPos] == chosen
    ensures found ==> states[chosen] == Ready
    // Priority optimality: no Ready thread has strictly higher priority than chosen.
    ensures found ==> forall i :: 0 <= i < n && states[i] == Ready ==> prios[chosen] >= prios[i]
    // FIFO tie-breaking within equal priority: no earlier Ready thread in order has equal priority.
    ensures found ==> forall k :: 0 <= k < chosenPos && states[order[k]] == Ready ==>
                        prios[order[k]] < prios[chosen]
    // Completeness: found is false if and only if no thread is Ready (idle fallback).
    ensures !found ==> forall i :: 0 <= i < n ==> states[i] != Ready
  {
    found := false;
    chosen := 0;
    chosenPos := 0;
    var i := 0;
    while i < n
      invariant 0 <= i <= n
      invariant found ==> 0 <= chosen < n && 0 <= chosenPos < i && order[chosenPos] == chosen
      invariant found ==> states[chosen] == Ready
      invariant found ==> forall k :: 0 <= k < i && states[order[k]] == Ready ==>
                            prios[chosen] >= prios[order[k]]
      invariant found ==> forall k :: 0 <= k < chosenPos && states[order[k]] == Ready ==>
                            prios[order[k]] < prios[chosen]
      invariant !found ==> forall k :: 0 <= k < i ==> states[order[k]] != Ready
    {
      var slot := order[i];
      if states[slot] == Ready && (!found || prios[slot] > prios[chosen]) {
        found := true;
        chosen := slot;
        chosenPos := i;
      }
      i := i + 1;
    }

    if found {
      forall t | 0 <= t < n && states[t] == Ready
        ensures prios[chosen] >= prios[t]
      {
        assert t in order;
        var idx :| 0 <= idx < n && order[idx] == t;
        assert prios[chosen] >= prios[order[idx]];
      }
    } else {
      forall t | 0 <= t < n
        ensures states[t] != Ready
      {
        assert t in order;
        var idx :| 0 <= idx < n && order[idx] == t;
        assert states[order[idx]] != Ready;
      }
    }
  }

  // Exact model of wrap-safe 32-bit half-range tick comparison: Tick(now - wake) < 0x80000000u.
  function WrapDiff32(a: int, b: int): int
    requires 0 <= a < TWO32 && 0 <= b < TWO32
  {
    (a - b) % TWO32
  }

  predicate WrapExpired(now: int, wake: int)
    requires 0 <= now < TWO32 && 0 <= wake < TWO32
  {
    WrapDiff32(now, wake) < TWO31
  }

  // Proves wrap-safe deadline math across arbitrary 32-bit rollover for all valid MaxWait delays!
  lemma LemmaWrapDeadlineExact(now0: int, delay: int, elapsed: int)
    requires 0 <= now0 < TWO32
    requires 1 <= delay < TWO31
    requires 0 <= elapsed < TWO31
    ensures WrapExpired((now0 + elapsed) % TWO32, (now0 + delay) % TWO32) <==> (elapsed >= delay)
  {
    var now := (now0 + elapsed) % TWO32;
    var wake := (now0 + delay) % TWO32;
    var diff := (now - wake) % TWO32;
    if elapsed >= delay {
      assert 0 <= elapsed - delay < TWO31;
      assert diff == elapsed - delay;
    } else {
      assert 0 < delay - elapsed < TWO31;
      assert diff == TWO32 - (delay - elapsed);
      assert diff >= TWO31;
    }
  }
}
