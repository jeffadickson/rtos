// SPDX-License-Identifier: BSD-2-Clause
// AXI4-Lite Parallel 16-bit CFI/JEDEC NOR Flash Controller for NexRT RTOS_STORAGE:
//   - Infineon S29GL128S / Micron MT28EW128ABA
// Drives asynchronous 16-bit word address + CE#/OE#/WE#/RESET#/DQ[15:0]/RY_BY#
// with programmable setup/pulse/hold wait states and transfer-complete interrupt.
`timescale 1ns/1ps

module nexrt_cfi_nor_axi #(
    parameter logic [31:0] BASE_ADDR = 32'h1002_2000,
    parameter int unsigned ADDR_BITS = 24
) (
    input  logic                 clk,
    input  logic                 rst_n,

    // AXI4-Lite slave interface
    input  logic [31:0]          s_axi_awaddr,
    input  logic                 s_axi_awvalid,
    output logic                 s_axi_awready,
    input  logic [31:0]          s_axi_wdata,
    input  logic [3:0]           s_axi_wstrb,
    input  logic                 s_axi_wvalid,
    output logic                 s_axi_wready,
    output logic [1:0]           s_axi_bresp,
    output logic                 s_axi_bvalid,
    input  logic                 s_axi_bready,

    input  logic [31:0]          s_axi_araddr,
    input  logic                 s_axi_arvalid,
    output logic                 s_axi_arready,
    output logic [31:0]          s_axi_rdata,
    output logic [1:0]           s_axi_rresp,
    output logic                 s_axi_rvalid,
    input  logic                 s_axi_rready,

    // External 16-bit Parallel CFI NOR pins (split DQ for FPGA IOBUF)
    output logic [ADDR_BITS-1:0] nor_addr,
    output logic                 nor_ce_n,
    output logic                 nor_oe_n,
    output logic                 nor_we_n,
    output logic                 nor_reset_n,
    output logic                 nor_wp_n,
    output logic [15:0]          nor_dq_out,
    output logic                 nor_dq_oe,
    input  logic [15:0]          nor_dq_in,
    input  logic                 nor_ry_by_n,
    output logic                 irq_done
);
    localparam logic [15:0] NOR_MAGIC = 16'h4346; // "CF"

    logic        reg_wr, reg_rd, reg_err;
    logic [11:0] reg_addr;
    logic [31:0] reg_wdata, reg_rdata;

    nexrt_axi_lite_slave #(
        .BASE_ADDR (BASE_ADDR),
        .WIN_BITS  (12)
    ) u_axi (
        .clk           (clk),
        .rst_n         (rst_n),
        .s_axi_awaddr  (s_axi_awaddr),
        .s_axi_awvalid (s_axi_awvalid),
        .s_axi_awready (s_axi_awready),
        .s_axi_wdata   (s_axi_wdata),
        .s_axi_wstrb   (s_axi_wstrb),
        .s_axi_wvalid  (s_axi_wvalid),
        .s_axi_wready  (s_axi_wready),
        .s_axi_bresp   (s_axi_bresp),
        .s_axi_bvalid  (s_axi_bvalid),
        .s_axi_bready  (s_axi_bready),
        .s_axi_araddr  (s_axi_araddr),
        .s_axi_arvalid (s_axi_arvalid),
        .s_axi_arready (s_axi_arready),
        .s_axi_rdata   (s_axi_rdata),
        .s_axi_rresp   (s_axi_rresp),
        .s_axi_rvalid  (s_axi_rvalid),
        .s_axi_rready  (s_axi_rready),
        .reg_wr        (reg_wr),
        .reg_rd        (reg_rd),
        .reg_addr      (reg_addr),
        .reg_wdata     (reg_wdata),
        .reg_rdata     (reg_rdata),
        .reg_err       (reg_err)
    );

    logic                 reset_n_reg;
    logic                 wp_n_reg;
    logic                 irq_en;
    logic                 irq_stat;
    logic [ADDR_BITS-1:0] addr_reg;
    logic [15:0]          wdata_reg;
    logic [15:0]          rdata_reg;
    logic [7:0]           t_setup, t_pulse, t_hold;
    logic                 ry_sync0, ry_sync1;

    typedef enum logic [1:0] {
        ST_IDLE  = 2'd0,
        ST_SETUP = 2'd1,
        ST_PULSE = 2'd2,
        ST_HOLD  = 2'd3
    } fsm_t;

    fsm_t       state;
    logic       is_write;
    logic [7:0] timer;

    wire busy = (state != ST_IDLE);

    assign nor_addr    = addr_reg;
    assign nor_ce_n    = !busy;
    assign nor_oe_n    = !(state == ST_PULSE && !is_write);
    assign nor_we_n    = !(state == ST_PULSE &&  is_write);
    assign nor_reset_n = reset_n_reg;
    assign nor_wp_n    = wp_n_reg;
    assign nor_dq_out  = wdata_reg;
    assign nor_dq_oe   = busy && is_write;
    assign irq_done    = irq_en && irq_stat;

    always_comb begin
        reg_rdata = 32'd0;
        reg_err   = 1'b0;
        unique case (reg_addr)
            12'h000: reg_rdata = {29'd0, irq_en, wp_n_reg, reset_n_reg};
            12'h004: reg_rdata = {NOR_MAGIC, 14'd0, ry_sync1, busy};
            12'h008: reg_rdata = {{(32-ADDR_BITS){1'b0}}, addr_reg};
            12'h00C: reg_rdata = {16'd0, wdata_reg};
            12'h010: reg_rdata = 32'd0;
            12'h014: reg_rdata = {16'd0, rdata_reg};
            12'h018: reg_rdata = {8'd0, t_hold, t_pulse, t_setup};
            12'h01C: reg_rdata = {31'd0, irq_stat};
            default: begin
                reg_rdata = 32'd0;
                reg_err   = (reg_wr || reg_rd);
            end
        endcase
    end

    wire _unused_nor = &{1'b0, reg_wdata[31:24]};

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reset_n_reg <= 1'b1;
            wp_n_reg    <= 1'b1;
            irq_en      <= 1'b0;
            irq_stat    <= 1'b0;
            addr_reg    <= '0;
            wdata_reg   <= 16'd0;
            rdata_reg   <= 16'd0;
            t_setup     <= 8'd1;
            t_pulse     <= 8'd2;
            t_hold      <= 8'd1;
            ry_sync0    <= 1'b1;
            ry_sync1    <= 1'b1;
            state       <= ST_IDLE;
            is_write    <= 1'b0;
            timer       <= 8'd0;
        end else begin
            ry_sync0 <= nor_ry_by_n;
            ry_sync1 <= ry_sync0;

            if (reg_wr && !reg_err) begin
                unique case (reg_addr)
                    12'h000: begin
                        reset_n_reg <= reg_wdata[0];
                        wp_n_reg    <= reg_wdata[1];
                        irq_en      <= reg_wdata[2];
                    end
                    12'h008: if (!busy) addr_reg <= reg_wdata[ADDR_BITS-1:0];
                    12'h00C: if (!busy) begin
                        wdata_reg <= reg_wdata[15:0];
                        is_write  <= 1'b1;
                        state     <= ST_SETUP;
                        timer     <= (t_setup == 8'd0) ? 8'd0 : (t_setup - 8'd1);
                    end
                    12'h010: if (!busy) begin
                        is_write <= 1'b0;
                        state    <= ST_SETUP;
                        timer    <= (t_setup == 8'd0) ? 8'd0 : (t_setup - 8'd1);
                    end
                    12'h018: begin
                        t_setup <= (reg_wdata[7:0]   == 8'd0) ? 8'd1 : reg_wdata[7:0];
                        t_pulse <= (reg_wdata[15:8]  == 8'd0) ? 8'd1 : reg_wdata[15:8];
                        t_hold  <= (reg_wdata[23:16] == 8'd0) ? 8'd1 : reg_wdata[23:16];
                    end
                    12'h01C: if (reg_wdata[0]) irq_stat <= 1'b0;
                    default: ;
                endcase
            end

            unique case (state)
                ST_IDLE: ;
                ST_SETUP: begin
                    if (timer != 8'd0) timer <= timer - 8'd1;
                    else begin
                        state <= ST_PULSE;
                        timer <= (t_pulse == 8'd0) ? 8'd0 : (t_pulse - 8'd1);
                    end
                end
                ST_PULSE: begin
                    if (timer != 8'd0) timer <= timer - 8'd1;
                    else begin
                        if (!is_write) rdata_reg <= nor_dq_in;
                        state <= ST_HOLD;
                        timer <= (t_hold == 8'd0) ? 8'd0 : (t_hold - 8'd1);
                    end
                end
                ST_HOLD: begin
                    if (timer != 8'd0) timer <= timer - 8'd1;
                    else begin
                        state    <= ST_IDLE;
                        irq_stat <= 1'b1;
                    end
                end
            endcase
        end
    end
endmodule
