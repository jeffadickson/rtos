// SPDX-License-Identifier: BSD-2-Clause
// AXI4-Lite 32-bit wrapper for nexrt_hw_sched, drop-in compatible with
// riscue_demo.sv / RISCue AXI interconnects.
`timescale 1ns/1ps

module nexrt_hw_sched_axi #(
    parameter int unsigned MAX_THREADS    = 16,
    parameter int unsigned MAX_IRQ_ROUTES = 4,
    parameter bit          TMR_ENABLE     = 1'b1,
    parameter logic [31:0] BASE_ADDR      = 32'h1001_0000
) (
    input  logic        clk,
    input  logic        rst_n,

    // AXI4-Lite slave interface
    input  logic [31:0] s_axi_awaddr,
    input  logic        s_axi_awvalid,
    output logic        s_axi_awready,
    input  logic [31:0] s_axi_wdata,
    input  logic [3:0]  s_axi_wstrb,
    input  logic        s_axi_wvalid,
    output logic        s_axi_wready,
    output logic [1:0]  s_axi_bresp,
    output logic        s_axi_bvalid,
    input  logic        s_axi_bready,

    input  logic [31:0] s_axi_araddr,
    input  logic        s_axi_arvalid,
    output logic        s_axi_arready,
    output logic [31:0] s_axi_rdata,
    output logic [1:0]  s_axi_rresp,
    output logic        s_axi_rvalid,
    input  logic        s_axi_rready,

    input  logic [MAX_IRQ_ROUTES-1:0] periph_irq,
    output logic        sched_irq,
    output logic [3:0]  active_tid,
    output logic        active_valid,
    output logic [3:0]  next_tid_out,
    output logic        next_valid_out,
    output logic        seu_corrected_pulse,
    output logic        seu_fault_out,

    input  logic        fi_valid,
    input  logic        fi_double,
    input  logic [3:0]  fi_slot,
    input  logic [15:0] fi_mask
);

    logic        have_aw, have_w;
    logic [31:0] latched_aw;
    logic [31:0] latched_w;
    logic [3:0]  latched_strb;

    logic        core_wr;
    logic        core_rd;
    logic [9:0]  core_addr;
    logic [31:0] core_rdata;

    assign s_axi_awready = rst_n && !have_aw && !s_axi_bvalid;
    assign s_axi_wready  = rst_n && !have_w  && !s_axi_bvalid;
    assign s_axi_arready = rst_n && !s_axi_rvalid;

    wire do_write  = have_aw && have_w && !s_axi_bvalid;
    wire do_read   = s_axi_arvalid && s_axi_arready;
    wire wr_in_win = (latched_aw[31:10] == BASE_ADDR[31:10]) && (latched_aw[1:0] == 2'b00) && (latched_strb == 4'hF);
    wire rd_in_win = (s_axi_araddr[31:10] == BASE_ADDR[31:10]) && (s_axi_araddr[1:0] == 2'b00);

    assign core_wr   = do_write && wr_in_win;
    assign core_rd   = do_read  && rd_in_win;
    assign core_addr = core_wr ? latched_aw[9:0] : s_axi_araddr[9:0];

    nexrt_hw_sched #(
        .MAX_THREADS    (MAX_THREADS),
        .MAX_IRQ_ROUTES (MAX_IRQ_ROUTES),
        .TMR_ENABLE     (TMR_ENABLE)
    ) u_core (
        .clk                 (clk),
        .rst_n               (rst_n),
        .bus_wr              (core_wr),
        .bus_rd              (core_rd),
        .bus_addr            (core_addr),
        .bus_wdata           (latched_w),
        .bus_rdata           (core_rdata),
        .periph_irq          (periph_irq),
        .sched_irq           (sched_irq),
        .active_tid          (active_tid),
        .active_valid        (active_valid),
        .next_tid_out        (next_tid_out),
        .next_valid_out      (next_valid_out),
        .seu_corrected_pulse (seu_corrected_pulse),
        .seu_fault_out       (seu_fault_out),
        .fi_valid            (fi_valid),
        .fi_double           (fi_double),
        .fi_slot             (fi_slot),
        .fi_mask             (fi_mask)
    );

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            have_aw      <= 1'b0;
            have_w       <= 1'b0;
            latched_aw   <= 32'd0;
            latched_w    <= 32'd0;
            latched_strb <= 4'd0;
            s_axi_bvalid <= 1'b0;
            s_axi_bresp  <= 2'b00;
            s_axi_rvalid <= 1'b0;
            s_axi_rresp  <= 2'b00;
            s_axi_rdata  <= 32'd0;
        end else begin
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 1'b0;
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 1'b0;

            if (s_axi_awvalid && s_axi_awready) begin
                latched_aw <= s_axi_awaddr;
                have_aw    <= 1'b1;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                latched_w    <= s_axi_wdata;
                latched_strb <= s_axi_wstrb;
                have_w       <= 1'b1;
            end
            if (do_write) begin
                have_aw      <= 1'b0;
                have_w       <= 1'b0;
                s_axi_bvalid <= 1'b1;
                s_axi_bresp  <= wr_in_win ? 2'b00 : 2'b10;
            end
            if (do_read) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp  <= rd_in_win ? 2'b00 : 2'b10;
                s_axi_rdata  <= rd_in_win ? core_rdata : 32'd0;
            end
        end
    end

endmodule
