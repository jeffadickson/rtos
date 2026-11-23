// SPDX-License-Identifier: BSD-2-Clause
// NexRT-HWSched v1: Static 16-thread Hardware Scheduler Core with:
//   - 1-cycle combinational priority + starvation-free per-priority round-robin arbiter
//   - Parallel 64-bit hardware deadline comparators (true tickless scheduling)
//   - 1-cycle peripheral IRQ-to-thread wake routing
//   - Per-thread hardware residency counters & WCET budget watchdog
//   - Optional Triple-Modular Redundancy (TMR) with automatic 1-cycle scrubbing
//     and fault-injection hooks for SEU qualification on RISCue FPGA targets.
//
// Register map matches include/rtos_hw_sched.hpp byte-for-byte.
`timescale 1ns/1ps

module nexrt_hw_sched #(
    parameter int unsigned MAX_THREADS    = 16,
    parameter int unsigned MAX_IRQ_ROUTES = 4,
    parameter bit          TMR_ENABLE     = 1'b1
) (
    input  logic        clk,
    input  logic        rst_n,

    // Simple 32-bit synchronous register bus (wrapped by nexrt_hw_sched_axi for AXI4-Lite)
    input  logic        bus_wr,
    input  logic        bus_rd,
    input  logic [9:0]  bus_addr,
    input  logic [31:0] bus_wdata,
    output logic [31:0] bus_rdata,

    // External peripheral IRQ lines (active-high, 1-cycle wake routing)
    input  logic [MAX_IRQ_ROUTES-1:0] periph_irq,

    // Hardware scheduler outputs to RISCue core / banked register file
    output logic        sched_irq,
    output logic [3:0]  active_tid,
    output logic        active_valid,
    output logic [3:0]  next_tid_out,
    output logic        next_valid_out,
    output logic        seu_corrected_pulse,
    output logic        seu_fault_out,

    // SEU fault-injection interface (tie low in production bitstreams)
    input  logic        fi_valid,
    input  logic        fi_double,      // 0 = single-replica SEU (correctable), 1 = dual-replica
    input  logic [3:0]  fi_slot,
    input  logic [15:0] fi_mask
);

    localparam logic [31:0] VERSION_WORD = 32'h4E58_5301; // "NXS\x01"

    // Thread states matching rtos::State (3=finished is treated as non-runnable)
    localparam logic [2:0] ST_UNUSED      = 3'd0;
    localparam logic [2:0] ST_READY       = 3'd1;
    localparam logic [2:0] ST_SLEEPING    = 3'd2;
    localparam logic [2:0] ST_BLOCKED     = 3'd4;
    localparam logic [2:0] ST_QUARANTINED = 3'd5;

    // -------------------------------------------------------------------------
    // TMR-protected critical metadata (state + eff_prio + ctrl + current + order)
    // -------------------------------------------------------------------------
    // Per-thread critical word: {eff_prio[7:0], base_prio[7:0], state[2:0]} = 19 bits
    logic [18:0] thr_a [0:MAX_THREADS-1];
    logic [18:0] thr_b [0:MAX_THREADS-1];
    logic [18:0] thr_c [0:MAX_THREADS-1];
    logic [18:0] thr_v [0:MAX_THREADS-1];

    // Order permutation (16 x 4 bits = 64 bits), TMR replicated
    logic [63:0] order_a, order_b, order_c, order_v;

    // Control + current state word: {current_valid, current_tid[3:0], ctrl[4:0]} = 10 bits
    logic [9:0]  sys_a, sys_b, sys_c, sys_v;

    // Non-TMR counters/deadlines (still deterministic, bounded static storage)
    logic [63:0] wake_r     [0:MAX_THREADS-1];
    logic [31:0] budget_r   [0:MAX_THREADS-1];
    logic [63:0] runtime_r  [0:MAX_THREADS-1];
    logic [31:0] dispatch_r [0:MAX_THREADS-1];
    logic [31:0] slice_r    [0:MAX_THREADS-1];

    logic [3:0]  route_tid_r [0:MAX_IRQ_ROUTES-1];
    logic        route_en_r  [0:MAX_IRQ_ROUTES-1];
    logic        route_pnd_r [0:MAX_IRQ_ROUTES-1];

    logic [31:0] quantum_r;
    logic [31:0] slice_remain_r;
    logic [15:0] expired_mask_r;
    logic [15:0] fault_mask_r;
    logic [31:0] seu_count_r;
    logic [63:0] mtime_r;
    logic        slice_expired_r;
    logic        seu_fault_r;

    // Majority voter helper
    function automatic logic [18:0] vote19(input logic [18:0] a, input logic [18:0] b, input logic [18:0] c);
        return (a & b) | (b & c) | (a & c);
    endfunction

    function automatic logic [63:0] vote64(input logic [63:0] a, input logic [63:0] b, input logic [63:0] c);
        return (a & b) | (b & c) | (a & c);
    endfunction

    function automatic logic [9:0] vote10(input logic [9:0] a, input logic [9:0] b, input logic [9:0] c);
        return (a & b) | (b & c) | (a & c);
    endfunction

    // Voted views & SEU mismatch detection
    logic [MAX_THREADS-1:0] thr_mismatch;
    logic                   order_mismatch;
    logic                   sys_mismatch;
    logic                   any_seu_mismatch;

    always_comb begin
        for (int i = 0; i < MAX_THREADS; i++) begin
            if (TMR_ENABLE) begin
                thr_v[i]        = vote19(thr_a[i], thr_b[i], thr_c[i]);
                thr_mismatch[i] = ((thr_a[i] ^ thr_b[i]) | (thr_b[i] ^ thr_c[i])) != 19'd0;
            end else begin
                thr_v[i]        = thr_a[i];
                thr_mismatch[i] = 1'b0;
            end
        end
        if (TMR_ENABLE) begin
            order_v        = vote64(order_a, order_b, order_c);
            order_mismatch = ((order_a ^ order_b) | (order_b ^ order_c)) != 64'd0;
            sys_v          = vote10(sys_a, sys_b, sys_c);
            sys_mismatch   = ((sys_a ^ sys_b) | (sys_b ^ sys_c)) != 10'd0;
        end else begin
            order_v        = order_a;
            order_mismatch = 1'b0;
            sys_v          = sys_a;
            sys_mismatch   = 1'b0;
        end
        any_seu_mismatch = (|thr_mismatch) | order_mismatch | sys_mismatch;
    end

    wire [4:0] ctrl_v          = sys_v[4:0];
    wire [3:0] current_tid_v   = sys_v[8:5];
    wire       current_valid_v = sys_v[9];

    wire ctrl_enable   = ctrl_v[0];
    wire ctrl_tickless = ctrl_v[1];
    wire ctrl_rr       = ctrl_v[2];
    wire ctrl_irq_en   = ctrl_v[4];

    // -------------------------------------------------------------------------
    // 1-Cycle Combinational Priority + Fair Per-Priority Round-Robin Arbiter
    // -------------------------------------------------------------------------
    logic [3:0]  arb_next_tid;
    logic        arb_next_valid;
    logic [7:0]  arb_best_prio;
    logic [15:0] ready_mask_c;
    logic        ready_peers_c;
    logic        preempt_req_c;

    function automatic logic [63:0] rotate_order_back(input logic [63:0] cur_order, input logic [3:0] tid);
        logic [3:0] arr [0:MAX_THREADS-1];
        logic [3:0] out_arr [0:MAX_THREADS-1];
        logic [63:0] packed_out;
        int pos;
        for (int i = 0; i < MAX_THREADS; i++) begin
            arr[i]     = cur_order[i*4 +: 4];
            out_arr[i] = arr[i];
        end
        pos = MAX_THREADS;
        for (int i = 0; i < MAX_THREADS; i++) begin
            if (pos == MAX_THREADS && arr[i] == tid) pos = i;
        end
        if (pos < MAX_THREADS) begin
            for (int i = 0; i < MAX_THREADS - 1; i++) begin
                if (i >= pos) out_arr[i] = arr[i + 1];
            end
            out_arr[MAX_THREADS - 1] = tid;
        end
        for (int i = 0; i < MAX_THREADS; i++) begin
            packed_out[i*4 +: 4] = out_arr[i];
        end
        return packed_out;
    endfunction

    // Helper for arbitrating over an arbitrary order vector
    task automatic run_arbiter(
        input  logic [63:0] ord_vec,
        output logic [3:0]  best_tid,
        output logic        best_valid,
        output logic [7:0]  best_prio
    );
        logic [3:0] tid;
        logic [2:0] st;
        logic [7:0] ep;
        best_tid   = 4'd0;
        best_valid = 1'b0;
        best_prio  = 8'd0;
        for (int i = 0; i < MAX_THREADS; i++) begin
            tid = ord_vec[i*4 +: 4];
            st  = thr_v[tid][2:0];
            ep  = thr_v[tid][18:11];
            if (st == ST_READY && (!best_valid || ep > best_prio)) begin
                best_valid = 1'b1;
                best_tid   = tid;
                best_prio  = ep;
            end
        end
    endtask

    always_comb begin
        logic       cur_ready;
        logic       higher_ready;
        logic       rotate_due;
        logic [7:0] cur_prio;

        run_arbiter(order_v, arb_next_tid, arb_next_valid, arb_best_prio);

        ready_mask_c = 16'd0;
        for (int i = 0; i < MAX_THREADS; i++) begin
            if (thr_v[i][2:0] == ST_READY) ready_mask_c[i] = 1'b1;
        end

        cur_ready     = current_valid_v && (thr_v[current_tid_v][2:0] == ST_READY);
        cur_prio      = thr_v[current_tid_v][18:11];
        ready_peers_c = 1'b0;
        if (cur_ready) begin
            for (int i = 0; i < MAX_THREADS; i++) begin
                if (i[3:0] != current_tid_v &&
                    thr_v[i][2:0] == ST_READY &&
                    thr_v[i][18:11] == cur_prio) begin
                    ready_peers_c = 1'b1;
                end
            end
        end

        higher_ready  = arb_next_valid && (!cur_ready || arb_best_prio > cur_prio);
        rotate_due    = slice_expired_r && ctrl_rr && (!ctrl_tickless || ready_peers_c);
        preempt_req_c = ctrl_enable &&
                        (higher_ready ||
                         (!cur_ready && arb_next_valid) ||
                         (current_valid_v && !cur_ready) ||
                         rotate_due ||
                         (expired_mask_r != 16'd0) ||
                         (fault_mask_r != 16'd0));
    end

    assign sched_irq           = ctrl_enable && ctrl_irq_en && preempt_req_c;
    assign active_tid          = current_tid_v;
    assign active_valid        = current_valid_v;
    assign next_tid_out        = arb_next_tid;
    assign next_valid_out      = arb_next_valid;
    assign seu_corrected_pulse = any_seu_mismatch;
    assign seu_fault_out       = seu_fault_r;

    // -------------------------------------------------------------------------
    // Combinational register read mux
    // -------------------------------------------------------------------------
    always_comb begin
        logic [3:0] rd_tid;
        logic [2:0] rd_field;
        rd_tid    = bus_addr[8:5];
        rd_field  = bus_addr[4:2];
        bus_rdata = 32'd0;
        if (bus_rd && bus_addr[1:0] == 2'b00) begin
            if (!bus_addr[9]) begin
                case (bus_addr[7:2])
                    6'h00: bus_rdata = {27'd0, ctrl_v};
                    6'h01: bus_rdata = {15'd0, seu_fault_r, (fault_mask_r != 16'd0), slice_expired_r,
                                        preempt_req_c, arb_next_valid, arb_next_tid, 3'd0,
                                        current_valid_v, current_tid_v};
                    6'h03: bus_rdata = quantum_r;
                    6'h04: bus_rdata = {16'd0, expired_mask_r};
                    6'h05: bus_rdata = {16'd0, ready_mask_c};
                    6'h06: bus_rdata = {16'd0, fault_mask_r};
                    6'h07: bus_rdata = seu_count_r;
                    6'h08: bus_rdata = {23'd0, route_pnd_r[0], 3'd0, route_en_r[0], route_tid_r[0]};
                    6'h09: bus_rdata = {23'd0, route_pnd_r[1], 3'd0, route_en_r[1], route_tid_r[1]};
                    6'h0A: bus_rdata = {23'd0, route_pnd_r[2], 3'd0, route_en_r[2], route_tid_r[2]};
                    6'h0B: bus_rdata = {23'd0, route_pnd_r[3], 3'd0, route_en_r[3], route_tid_r[3]};
                    6'h0C: bus_rdata = mtime_r[31:0];
                    6'h0D: bus_rdata = mtime_r[63:32];
                    6'h0E: bus_rdata = slice_remain_r;
                    6'h0F: bus_rdata = VERSION_WORD;
                    default: bus_rdata = 32'd0;
                endcase
            end else begin
                case (rd_field)
                    3'd0: bus_rdata = {8'd0, thr_v[rd_tid][18:11], thr_v[rd_tid][10:3], 5'd0, thr_v[rd_tid][2:0]};
                    3'd1: bus_rdata = wake_r[rd_tid][31:0];
                    3'd2: bus_rdata = wake_r[rd_tid][63:32];
                    3'd3: bus_rdata = budget_r[rd_tid];
                    3'd4: bus_rdata = runtime_r[rd_tid][31:0];
                    3'd5: bus_rdata = runtime_r[rd_tid][63:32];
                    3'd6: bus_rdata = dispatch_r[rd_tid];
                    3'd7: bus_rdata = slice_r[rd_tid];
                    default: bus_rdata = 32'd0;
                endcase
            end
        end
    end

    // -------------------------------------------------------------------------
    // Synchronous state update: TMR scrub + bus writes + 1-cycle hw step
    // -------------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (int i = 0; i < MAX_THREADS; i++) begin
                thr_a[i]      <= 19'd0;
                thr_b[i]      <= 19'd0;
                thr_c[i]      <= 19'd0;
                wake_r[i]     <= 64'hFFFF_FFFF_FFFF_FFFF;
                budget_r[i]   <= 32'd0;
                runtime_r[i]  <= 64'd0;
                dispatch_r[i] <= 32'd0;
                slice_r[i]    <= 32'd0;
            end
            order_a         <= 64'hFEDC_BA98_7654_3210;
            order_b         <= 64'hFEDC_BA98_7654_3210;
            order_c         <= 64'hFEDC_BA98_7654_3210;
            sys_a           <= 10'd0;
            sys_b           <= 10'd0;
            sys_c           <= 10'd0;
            for (int k = 0; k < MAX_IRQ_ROUTES; k++) begin
                route_tid_r[k] <= 4'd0;
                route_en_r[k]  <= 1'b0;
                route_pnd_r[k] <= 1'b0;
            end
            quantum_r       <= 32'd0;
            slice_remain_r  <= 32'd0;
            expired_mask_r  <= 16'd0;
            fault_mask_r    <= 16'd0;
            seu_count_r     <= 32'd0;
            mtime_r         <= 64'd0;
            slice_expired_r <= 1'b0;
            seu_fault_r     <= 1'b0;
        end else begin
            // Working next-state copies initialized from TMR-voted values (automatic scrubbing!)
            logic [18:0] thr_next [0:MAX_THREADS-1];
            logic [63:0] order_next;
            logic [9:0]  sys_next;
            logic [63:0] mtime_next;
            logic [15:0] expired_next;
            logic [15:0] fault_next;
            logic [31:0] slice_remain_next;
            logic        slice_expired_next;
            logic [3:0]  wr_tid;
            logic [2:0]  wr_field;
            logic [3:0]  commit_tid;
            logic        commit_valid;
            logic [7:0]  commit_prio;
            logic [3:0]  cur_t;
            logic        cur_v;

            for (int i = 0; i < MAX_THREADS; i++) thr_next[i] = thr_v[i];
            order_next         = order_v;
            sys_next           = sys_v;
            expired_next       = expired_mask_r;
            fault_next         = fault_mask_r;
            slice_remain_next  = slice_remain_r;
            slice_expired_next = slice_expired_r;

            // Count TMR-corrected single-event upsets (saturating)
            if (any_seu_mismatch && seu_count_r != 32'hFFFF_FFFF) begin
                seu_count_r <= seu_count_r + 32'd1;
            end

            // 1. Always advance hardware monotonic cycle counter
            mtime_next = mtime_r + 64'd1;
            mtime_r    <= mtime_next;

            // 2. Active hardware scheduler step (parallel comparators, IRQ wake, WCET watchdog)
            if (sys_next[0]) begin
                // Peripheral IRQ-to-thread 1-cycle wake routing
                for (int k = 0; k < MAX_IRQ_ROUTES; k++) begin
                    if (periph_irq[k] && route_en_r[k]) begin
                        route_pnd_r[k] <= 1'b1;
                        if (thr_next[route_tid_r[k]][2:0] == ST_SLEEPING ||
                            thr_next[route_tid_r[k]][2:0] == ST_BLOCKED) begin
                            thr_next[route_tid_r[k]][2:0] = ST_READY;
                        end
                    end
                end

                // Parallel hardware deadline comparators
                for (int i = 0; i < MAX_THREADS; i++) begin
                    if ((thr_next[i][2:0] == ST_SLEEPING || thr_next[i][2:0] == ST_BLOCKED) &&
                        mtime_next >= wake_r[i]) begin
                        expired_next[i] = 1'b1;
                        if (thr_next[i][2:0] == ST_SLEEPING) begin
                            thr_next[i][2:0] = ST_READY;
                        end
                    end
                end

                // Residency accounting, slice countdown, and WCET budget watchdog
                cur_t = sys_next[8:5];
                cur_v = sys_next[9];
                if (cur_v && thr_next[cur_t][2:0] == ST_READY) begin
                    runtime_r[cur_t] <= runtime_r[cur_t] + 64'd1;
                    slice_r[cur_t]   <= slice_r[cur_t] + 32'd1;
                    if (budget_r[cur_t] != 32'd0 && (slice_r[cur_t] + 32'd1) > budget_r[cur_t]) begin
                        thr_next[cur_t][2:0] = ST_QUARANTINED;
                        fault_next[cur_t]    = 1'b1;
                    end
                    if (sys_next[2] && quantum_r != 32'd0) begin
                        if (slice_remain_next > 32'd1) begin
                            slice_remain_next = slice_remain_next - 32'd1;
                        end else begin
                            slice_remain_next  = 32'd0;
                            slice_expired_next = 1'b1;
                        end
                    end
                end
            end

            // 3. Apply synchronous MMIO writes
            if (bus_wr && bus_addr[1:0] == 2'b00) begin
                if (!bus_addr[9]) begin
                    case (bus_addr[7:2])
                        6'h00: begin
                            // Preserve sealed bit once set
                            sys_next[4:0] = bus_wdata[4:0] | {1'b0, sys_v[3], 3'b000};
                            if (bus_wdata[0] && slice_remain_next == 32'd0 && quantum_r != 32'd0) begin
                                slice_remain_next = quantum_r;
                            end
                        end
                        6'h02: begin
                            // COMMIT register: optional rotate, then atomic commit of next_tid
                            if (bus_wdata[1] && sys_next[9]) begin
                                order_next = rotate_order_back(order_next, sys_next[8:5]);
                            end
                            if (bus_wdata[0]) begin
                                // Re-arbitrate on post-rotation order & updated thr_next
                                commit_tid   = 4'd0;
                                commit_valid = 1'b0;
                                commit_prio  = 8'd0;
                                for (int i = 0; i < MAX_THREADS; i++) begin
                                    logic [3:0] t_id;
                                    t_id = order_next[i*4 +: 4];
                                    if (thr_next[t_id][2:0] == ST_READY &&
                                        (!commit_valid || thr_next[t_id][18:11] > commit_prio)) begin
                                        commit_valid = 1'b1;
                                        commit_tid   = t_id;
                                        commit_prio  = thr_next[t_id][18:11];
                                    end
                                end
                                if (sys_next[9] && (!commit_valid || commit_tid != sys_next[8:5])) begin
                                    slice_r[sys_next[8:5]] <= 32'd0;
                                end
                                sys_next[8:5] = commit_tid;
                                sys_next[9]   = commit_valid;
                                if (commit_valid) begin
                                    dispatch_r[commit_tid] <= dispatch_r[commit_tid] + 32'd1;
                                    slice_r[commit_tid]    <= 32'd0;
                                end
                                slice_remain_next  = quantum_r;
                                slice_expired_next = 1'b0;
                            end
                        end
                        6'h03: begin
                            quantum_r <= bus_wdata;
                            if (slice_remain_next == 32'd0 || slice_remain_next > bus_wdata) begin
                                slice_remain_next = bus_wdata;
                            end
                        end
                        6'h04: expired_next = expired_next & ~bus_wdata[15:0];
                        6'h06: fault_next   = fault_next   & ~bus_wdata[15:0];
                        6'h08, 6'h09, 6'h0A, 6'h0B: begin
                            logic [1:0] ridx;
                            ridx = bus_addr[3:2];
                            if (bus_wdata[8]) route_pnd_r[ridx] <= 1'b0;
                            if (!sys_v[3]) begin
                                route_tid_r[ridx] <= bus_wdata[3:0];
                                route_en_r[ridx]  <= bus_wdata[4];
                            end
                        end
                        default: ;
                    endcase
                end else begin
                    wr_tid   = bus_addr[8:5];
                    wr_field = bus_addr[4:2];
                    case (wr_field)
                        3'd0: begin
                            if (!(sys_v[3] && thr_v[wr_tid][2:0] == ST_UNUSED && bus_wdata[2:0] != ST_UNUSED)) begin
                                thr_next[wr_tid] = {bus_wdata[23:16], bus_wdata[15:8], bus_wdata[2:0]};
                                if (bus_wdata[2:0] != ST_READY) slice_r[wr_tid] <= 32'd0;
                            end
                        end
                        3'd1: wake_r[wr_tid][31:0]  <= bus_wdata;
                        3'd2: wake_r[wr_tid][63:32] <= bus_wdata;
                        3'd3: budget_r[wr_tid]      <= bus_wdata;
                        default: ;
                    endcase
                end
            end

            // Commit non-TMR next-state registers cleanly
            expired_mask_r  <= expired_next;
            fault_mask_r    <= fault_next;
            slice_remain_r  <= slice_remain_next;
            slice_expired_r <= slice_expired_next;

            // 4. Write all 3 TMR replicas (+ optional SEU fault injection for qualification)
            for (int i = 0; i < MAX_THREADS; i++) begin
                if (fi_valid && (i[3:0] == fi_slot)) begin
                    thr_a[i] <= thr_next[i] ^ {3'b000, fi_mask};
                    thr_b[i] <= fi_double ? (thr_next[i] ^ {3'b000, fi_mask}) : thr_next[i];
                    thr_c[i] <= thr_next[i];
                end else begin
                    thr_a[i] <= thr_next[i];
                    thr_b[i] <= thr_next[i];
                    thr_c[i] <= thr_next[i];
                end
            end
            if (fi_valid && fi_double) begin
                seu_fault_r <= 1'b1;
            end
            order_a <= order_next;
            order_b <= order_next;
            order_c <= order_next;
            sys_a   <= sys_next;
            sys_b   <= sys_next;
            sys_c   <= sys_next;
        end
    end

endmodule
