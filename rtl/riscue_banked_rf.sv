// SPDX-License-Identifier: BSD-2-Clause
// RISCue-MT Phase 4: Banked Hardware Register File + Banked mepc/mstatus.
//
// Gives up to NUM_BANKS real-time threads dedicated hardware context banks
// indexed directly by NexRT-HWSched's active_tid. Switching between banked
// threads requires zero RAM GPR save/restore (1-cycle bank mux switch).
// Threads with tid >= NUM_BANKS assert spill_required and use RAM frames.
// Optional TMR_ENABLE=1 replicates all banks with 2-of-3 majority voting,
// 1-cycle scrubbing, and SEU fault-injection hooks.
`timescale 1ns/1ps

module riscue_banked_rf #(
    parameter int unsigned NUM_BANKS  = 8,
    parameter bit          TMR_ENABLE = 1'b1
) (
    input  logic        clk,
    input  logic        rst_n,

    // Active thread ID from NexRT-HWSched (0..15)
    input  logic [3:0]  active_tid,
    input  logic        active_valid,
    output logic        spill_required,

    // Standard RV32I dual-read, single-write GPR port
    input  logic [4:0]  rs1_addr,
    input  logic [4:0]  rs2_addr,
    output logic [31:0] rs1_data,
    output logic [31:0] rs2_data,
    input  logic        rd_we,
    input  logic [4:0]  rd_addr,
    input  logic [31:0] rd_wdata,

    // Banked trap CSRs (mepc / mstatus) for 1-cycle context restore
    input  logic        mepc_we,
    input  logic [31:0] mepc_wdata,
    output logic [31:0] mepc_rdata,
    input  logic        mstatus_we,
    input  logic [31:0] mstatus_wdata,
    output logic [31:0] mstatus_rdata,

    // SEU telemetry & fault injection
    output logic [31:0] seu_count,
    output logic        seu_fault,
    input  logic        fi_valid,
    input  logic        fi_double,
    input  logic [2:0]  fi_bank,
    input  logic [4:0]  fi_reg,
    input  logic [31:0] fi_mask
);

    localparam int unsigned BANK_BITS = $clog2(NUM_BANKS);

    wire [BANK_BITS-1:0] bank_idx = active_tid[BANK_BITS-1:0];
    assign spill_required = !active_valid || ({28'd0, active_tid} >= NUM_BANKS);

    // 31 GPRs per bank (index 1..31; x0 is hardwired to 0) + mepc + mstatus
    logic [31:0] rf_a [0:NUM_BANKS-1][0:31];
    logic [31:0] rf_b [0:NUM_BANKS-1][0:31];
    logic [31:0] rf_c [0:NUM_BANKS-1][0:31];

    logic [31:0] mepc_a [0:NUM_BANKS-1];
    logic [31:0] mepc_b [0:NUM_BANKS-1];
    logic [31:0] mepc_c [0:NUM_BANKS-1];

    logic [31:0] mstat_a [0:NUM_BANKS-1];
    logic [31:0] mstat_b [0:NUM_BANKS-1];
    logic [31:0] mstat_c [0:NUM_BANKS-1];

    logic [31:0] seu_count_r;
    logic        seu_fault_r;

    function automatic logic [31:0] vote32(input logic [31:0] a, input logic [31:0] b, input logic [31:0] c);
        return (a & b) | (b & c) | (a & c);
    endfunction

    // Combinational voted reads (1-cycle bank switch via bank_idx)
    always_comb begin
        if (rs1_addr == 5'd0) begin
            rs1_data = 32'd0;
        end else if (TMR_ENABLE) begin
            rs1_data = vote32(rf_a[bank_idx][rs1_addr], rf_b[bank_idx][rs1_addr], rf_c[bank_idx][rs1_addr]);
        end else begin
            rs1_data = rf_a[bank_idx][rs1_addr];
        end

        if (rs2_addr == 5'd0) begin
            rs2_data = 32'd0;
        end else if (TMR_ENABLE) begin
            rs2_data = vote32(rf_a[bank_idx][rs2_addr], rf_b[bank_idx][rs2_addr], rf_c[bank_idx][rs2_addr]);
        end else begin
            rs2_data = rf_a[bank_idx][rs2_addr];
        end

        if (TMR_ENABLE) begin
            mepc_rdata    = vote32(mepc_a[bank_idx], mepc_b[bank_idx], mepc_c[bank_idx]);
            mstatus_rdata = vote32(mstat_a[bank_idx], mstat_b[bank_idx], mstat_c[bank_idx]);
        end else begin
            mepc_rdata    = mepc_a[bank_idx];
            mstatus_rdata = mstat_a[bank_idx];
        end
    end

    assign seu_count = seu_count_r;
    assign seu_fault = seu_fault_r;

    // Background 1-register-per-cycle TMR scrubber across active bank
    logic [4:0] scrub_reg;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (int b = 0; b < NUM_BANKS; b++) begin
                for (int r = 0; r < 32; r++) begin
                    rf_a[b][r] <= 32'd0;
                    rf_b[b][r] <= 32'd0;
                    rf_c[b][r] <= 32'd0;
                end
                mepc_a[b]  <= 32'd0;
                mepc_b[b]  <= 32'd0;
                mepc_c[b]  <= 32'd0;
                mstat_a[b] <= 32'h0000_1880; // MPP=M, MPIE=1
                mstat_b[b] <= 32'h0000_1880;
                mstat_c[b] <= 32'h0000_1880;
            end
            scrub_reg   <= 5'd1;
            seu_count_r <= 32'd0;
            seu_fault_r <= 1'b0;
        end else begin
            // Scrub one GPR per cycle across all banks
            logic any_mismatch;
            any_mismatch = 1'b0;
            if (TMR_ENABLE) begin
                for (int b = 0; b < NUM_BANKS; b++) begin
                    logic [31:0] va, vb, vc, vv;
                    va = rf_a[b][scrub_reg];
                    vb = rf_b[b][scrub_reg];
                    vc = rf_c[b][scrub_reg];
                    if (((va ^ vb) | (vb ^ vc)) != 32'd0) begin
                        vv = vote32(va, vb, vc);
                        rf_a[b][scrub_reg] <= vv;
                        rf_b[b][scrub_reg] <= vv;
                        rf_c[b][scrub_reg] <= vv;
                        any_mismatch = 1'b1;
                    end
                end
                if (any_mismatch && seu_count_r != 32'hFFFF_FFFF) begin
                    seu_count_r <= seu_count_r + 32'd1;
                end
            end
            scrub_reg <= (scrub_reg == 5'd31) ? 5'd1 : (scrub_reg + 5'd1);

            // Normal writes to active bank (x0 writes ignored)
            if (rd_we && rd_addr != 5'd0) begin
                rf_a[bank_idx][rd_addr] <= rd_wdata;
                rf_b[bank_idx][rd_addr] <= rd_wdata;
                rf_c[bank_idx][rd_addr] <= rd_wdata;
            end
            if (mepc_we) begin
                mepc_a[bank_idx] <= mepc_wdata;
                mepc_b[bank_idx] <= mepc_wdata;
                mepc_c[bank_idx] <= mepc_wdata;
            end
            if (mstatus_we) begin
                mstat_a[bank_idx] <= mstatus_wdata;
                mstat_b[bank_idx] <= mstatus_wdata;
                mstat_c[bank_idx] <= mstatus_wdata;
            end

            // Optional SEU fault injection
            if (fi_valid && ({29'd0, fi_bank} < NUM_BANKS) && fi_reg != 5'd0) begin
                rf_a[fi_bank[BANK_BITS-1:0]][fi_reg] <= rf_a[fi_bank[BANK_BITS-1:0]][fi_reg] ^ fi_mask;
                if (fi_double) begin
                    rf_b[fi_bank[BANK_BITS-1:0]][fi_reg] <= rf_b[fi_bank[BANK_BITS-1:0]][fi_reg] ^ fi_mask;
                    seu_fault_r <= 1'b1;
                end
            end
        end
    end

endmodule
