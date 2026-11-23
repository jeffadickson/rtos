// SPDX-License-Identifier: BSD-2-Clause
// Formal Dafny bitvector proof of the 2-of-3 TMR majority voter and 1-cycle
// scrubber used in rtl/nexrt_hw_sched.sv and rtl/riscue_banked_rf.sv.

module NexRTTmrVoterProof {

  function {:opaque} Vote32(a: bv32, b: bv32, c: bv32): bv32
  {
    (a & b) | (b & c) | (a & c)
  }

  function {:opaque} Mismatch32(a: bv32, b: bv32, c: bv32): bool
  {
    ((a ^ b) | (b ^ c)) != 0
  }

  // Theorem 1: ANY arbitrary 32-bit corruption of ANY single replica (A, B, or C)
  // is 100% masked by the bitwise majority voter!
  lemma TheoremSingleReplicaSeuMasked(truth: bv32, corrupted: bv32)
    ensures Vote32(corrupted, truth, truth) == truth
    ensures Vote32(truth, corrupted, truth) == truth
    ensures Vote32(truth, truth, corrupted) == truth
  {
    reveal Vote32();
  }

  // Corollary: Expresses Theorem 1 directly in XOR bit-flip mask form (truth ^ mask).
  lemma CorollaryXorBitFlipMasked(truth: bv32, mask: bv32)
    ensures Vote32(truth ^ mask, truth, truth) == truth
    ensures Vote32(truth, truth ^ mask, truth) == truth
    ensures Vote32(truth, truth, truth ^ mask) == truth
  {
    TheoremSingleReplicaSeuMasked(truth, truth ^ mask);
  }

  // Theorem 2: The mismatch detector detects ANY non-zero single-replica upset,
  // and is false when all three replicas agree.
  lemma TheoremScrubDetectorExact(truth: bv32, corrupted: bv32)
    ensures !Mismatch32(truth, truth, truth)
    ensures corrupted != truth ==> Mismatch32(corrupted, truth, truth)
    ensures corrupted != truth ==> Mismatch32(truth, corrupted, truth)
    ensures corrupted != truth ==> Mismatch32(truth, truth, corrupted)
  {
    reveal Mismatch32();
  }

  // Theorem 3: One-cycle scrubbing restores exact triple equality (truth, truth, truth)
  // after ANY single-replica upset, with zero bitvector explosion.
  method ScrubCycle(a: bv32, b: bv32, c: bv32, ghost truth: bv32)
      returns (na: bv32, nb: bv32, nc: bv32, corrected: bool)
    requires (a == truth && b == truth) || (b == truth && c == truth) || (a == truth && c == truth)
    ensures na == truth && nb == truth && nc == truth
    ensures !Mismatch32(na, nb, nc)
    ensures corrected <==> Mismatch32(a, b, c)
  {
    if a == truth && b == truth {
      TheoremSingleReplicaSeuMasked(truth, c);
    } else if b == truth && c == truth {
      TheoremSingleReplicaSeuMasked(truth, a);
    } else {
      TheoremSingleReplicaSeuMasked(truth, b);
    }
    TheoremScrubDetectorExact(truth, truth);
    var voted := Vote32(a, b, c);
    corrected := Mismatch32(a, b, c);
    na := voted;
    nb := voted;
    nc := voted;
  }
}
