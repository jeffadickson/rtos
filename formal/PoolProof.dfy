// NexRT Formal Proof: Static Fixed-Size Block Pool (FixedPool / RTOS_POOL)
//
// Proves:
//   1. Conservation invariant: |freeSet| + |usedSet| == capacity at every state.
//   2. Zero aliasing: freeSet and usedSet are strictly disjoint subsets of 0..capacity-1.
//   3. Allocate returns a fresh valid slot in bounds and transitions it Free -> Used.
//   4. Release rejects out-of-range indices and double-frees fail-closed without mutating state.

module NexRTPoolProof {

  datatype Pool = Pool(
    capacity: nat,
    freeSet: set<nat>,
    usedSet: set<nat>
  )

  predicate ValidPool(p: Pool)
  {
    p.capacity > 0 &&
    p.freeSet !! p.usedSet &&
    (forall i: nat :: i in p.freeSet ==> i < p.capacity) &&
    (forall i: nat :: i in p.usedSet ==> i < p.capacity) &&
    (forall i: nat :: i < p.capacity ==> i in p.freeSet || i in p.usedSet) &&
    |p.freeSet| + |p.usedSet| == p.capacity
  }

  function RangeSet(n: nat): set<nat>
  {
    if n == 0 then {} else RangeSet(n - 1) + {n - 1}
  }

  lemma RangeSetProps(n: nat)
    ensures |RangeSet(n)| == n
    ensures forall i: nat :: i in RangeSet(n) <==> i < n
  {
    if n > 0 {
      RangeSetProps(n - 1);
      assert (n - 1) !in RangeSet(n - 1);
    }
  }

  function InitPool(cap: nat): Pool
    requires cap > 0
  {
    Pool(cap, RangeSet(cap), {})
  }

  lemma InitPoolIsValid(cap: nat)
    requires cap > 0
    ensures ValidPool(InitPool(cap))
    ensures |InitPool(cap).freeSet| == cap
    ensures |InitPool(cap).usedSet| == 0
  {
    RangeSetProps(cap);
  }

  // Allocate transitions one slot from freeSet -> usedSet while preserving conservation.
  method Allocate(p: Pool) returns (p': Pool, ok: bool, slot: nat)
    requires ValidPool(p)
    ensures ValidPool(p')
    ensures p'.capacity == p.capacity
    ensures ok <==> |p.freeSet| > 0
    ensures ok ==> slot < p.capacity && slot in p.freeSet && slot !in p.usedSet
    ensures ok ==> slot in p'.usedSet && slot !in p'.freeSet
    ensures ok ==> |p'.usedSet| == |p.usedSet| + 1 && |p'.freeSet| == |p.freeSet| - 1
    ensures !ok ==> p' == p
  {
    if |p.freeSet| == 0 {
      p' := p;
      ok := false;
      slot := 0;
    } else {
      var chosen :| chosen in p.freeSet;
      p' := Pool(p.capacity, p.freeSet - {chosen}, p.usedSet + {chosen});
      ok := true;
      slot := chosen;
    }
  }

  // Release transitions one valid used slot back to freeSet and rejects out-of-range or double-free!
  method Release(p: Pool, slot: nat) returns (p': Pool, ok: bool)
    requires ValidPool(p)
    ensures ValidPool(p')
    ensures p'.capacity == p.capacity
    ensures ok <==> (slot < p.capacity && slot in p.usedSet)
    ensures ok ==> slot in p'.freeSet && slot !in p'.usedSet
    ensures ok ==> |p'.freeSet| == |p.freeSet| + 1 && |p'.usedSet| == |p.usedSet| - 1
    ensures !ok ==> p' == p
  {
    if slot >= p.capacity || slot !in p.usedSet {
      p' := p;
      ok := false;
    } else {
      p' := Pool(p.capacity, p.freeSet + {slot}, p.usedSet - {slot});
      ok := true;
    }
  }

  // Theorem: Double-free is strictly rejected and leaves the pool state identical.
  method DoubleFreeIsRejected(p: Pool, slot: nat) returns (p1: Pool, p2: Pool, ok1: bool, ok2: bool)
    requires ValidPool(p)
    requires slot < p.capacity && slot in p.usedSet
    ensures ok1 && !ok2
    ensures p2 == p1
    ensures ValidPool(p2)
  {
    p1, ok1 := Release(p, slot);
    p2, ok2 := Release(p1, slot);
  }

}
