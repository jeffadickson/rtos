// SPDX-License-Identifier: BSD-2-Clause
// AXI4-Lite Parallel ONFI 1.0 x8 NAND Flash Controller for NexRT RTOS_STORAGE:
//   - Micron MT29F4G08ABA / Kioxia TC58NVG2S0H (CLE/ALE/WE#/RE#/CE#/WP#/DQ[7:0]/R/B#)
// Features:
//   - Programmable asynchronous setup / pulse / hold timing cycles
//   - 2-FF metastable-safe R/B# synchronizer with rising-edge ready interrupt
//   - Built-in hardware 22-bit complementary Hamming-256 SECDED ECC accelerator
//     bit-exact with rtos::storage::hamming256_encode()
`timescale 1ns/1ps

module nexrt_onfi_nand_axi #(
    parameter logic [31:0] BASE_ADDR  = 32'h1002_1000,
    parameter bit          ECC_ENABLE = 1'b1
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

    // External ONFI x8 NAND pins (split DQ for FPGA IOBUF instantiation)
    output logic        nand_ce_n,
    output logic        nand_cle,
    output logic        nand_ale,
    output logic        nand_we_n,
    output logic        nand_re_n,
    output logic        nand_wp_n,
    output logic [7:0]  nand_dq_out,
    output logic        nand_dq_oe,
    input  logic [7:0]  nand_dq_in,
    input  logic        nand_rb_n,
    output logic        irq_out
);
    localparam logic [15:0] NAND_MAGIC = 16'h4E41; // "NA"

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

    // Control, timing, and interrupt registers
    logic       ce_assert;
    logic       wp_n_reg;
    logic       irq_done_en;
    logic       irq_rb_en;
    logic       irq_done_stat;
    logic       irq_rb_stat;
    logic [7:0] t_setup;
    logic [7:0] t_pulse;
    logic [7:0] t_hold;

    // 2-FF synchronizer + rising edge detector for R/B#
    logic rb_sync0, rb_sync1, rb_prev;

    // Bus operation FSM
    typedef enum logic [1:0] {
        ST_IDLE  = 2'd0,
        ST_SETUP = 2'd1,
        ST_PULSE = 2'd2,
        ST_HOLD  = 2'd3
    } fsm_t;

    typedef enum logic [1:0] {
        OP_CMD  = 2'd0,
        OP_ADDR = 2'd1,
        OP_WR   = 2'd2,
        OP_RD   = 2'd3
    } op_t;

    fsm_t       state;
    op_t        cur_op;
    logic [7:0] timer;
    logic [7:0] tx_byte;
    logic [7:0] rx_byte;

    // Hardware 22-bit complementary Hamming-256 ECC engine
    logic [10:0] ecc_even;
    logic [10:0] ecc_odd;
    logic [8:0]  ecc_cnt; // 0..256

    function automatic logic [21:0] hamming_step(
        input logic [10:0] cur_even,
        input logic [10:0] cur_odd,
        input logic [7:0]  b,
        input logic [7:0]  byte_idx
    );
        logic       bp;
        logic [2:0] bit_odd, bit_even;
        logic [10:0] n_odd, n_even;
        bp          = ^b;
        bit_odd[0]  = b[1] ^ b[3] ^ b[5] ^ b[7];
        bit_odd[1]  = b[2] ^ b[3] ^ b[6] ^ b[7];
        bit_odd[2]  = b[4] ^ b[5] ^ b[6] ^ b[7];
        bit_even[0] = b[0] ^ b[2] ^ b[4] ^ b[6];
        bit_even[1] = b[0] ^ b[1] ^ b[4] ^ b[5];
        bit_even[2] = b[0] ^ b[1] ^ b[2] ^ b[3];
        n_odd       = cur_odd  ^ {(bp ? byte_idx : 8'd0), bit_odd};
        n_even      = cur_even ^ {(bp ? ~byte_idx : 8'd0), bit_even};
        return {n_odd, n_even};
    endfunction

    wire busy = (state != ST_IDLE);

    assign nand_ce_n   = !(ce_assert || busy);
    assign nand_wp_n   = wp_n_reg;
    assign nand_cle    = busy && (cur_op == OP_CMD);
    assign nand_ale    = busy && (cur_op == OP_ADDR);
    assign nand_we_n   = !(state == ST_PULSE && cur_op != OP_RD);
    assign nand_re_n   = !(state == ST_PULSE && cur_op == OP_RD);
    assign nand_dq_oe  = busy && (cur_op != OP_RD);
    assign nand_dq_out = tx_byte;
    assign irq_out     = (irq_done_en && irq_done_stat) || (irq_rb_en && irq_rb_stat);

    always_comb begin
        reg_rdata = 32'd0;
        reg_err   = 1'b0;
        unique case (reg_addr)
            12'h000: reg_rdata = {27'd0, irq_rb_en, irq_done_en, 1'b0, wp_n_reg, ce_assert};
            12'h004: reg_rdata = {NAND_MAGIC, ecc_cnt[7:0], 5'd0, ECC_ENABLE, rb_sync1, busy};
            12'h008: reg_rdata = {24'd0, tx_byte};
            12'h00C: reg_rdata = {24'd0, tx_byte};
            12'h010: reg_rdata = {24'd0, tx_byte};
            12'h014: reg_rdata = 32'd0;
            12'h018: reg_rdata = {24'd0, rx_byte};
            12'h01C: reg_rdata = {8'd0, t_hold, t_pulse, t_setup};
            12'h020: reg_rdata = {10'd0, ecc_odd, ecc_even};
            12'h024: reg_rdata = {30'd0, irq_rb_stat, irq_done_stat};
            default: begin
                reg_rdata = 32'd0;
                reg_err   = (reg_wr || reg_rd);
            end
        endcase
    end

    wire _unused_nand = &{1'b0, reg_wdata[31:24]};

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ce_assert     <= 1'b0;
            wp_n_reg      <= 1'b1;
            irq_done_en   <= 1'b0;
            irq_rb_en     <= 1'b0;
            irq_done_stat <= 1'b0;
            irq_rb_stat   <= 1'b0;
            t_setup       <= 8'd1;
            t_pulse       <= 8'd2;
            t_hold        <= 8'd1;
            rb_sync0      <= 1'b1;
            rb_sync1      <= 1'b1;
            rb_prev       <= 1'b1;
            state         <= ST_IDLE;
            cur_op        <= OP_CMD;
            timer         <= 8'd0;
            tx_byte       <= 8'd0;
            rx_byte       <= 8'd0;
            ecc_even      <= 11'd0;
            ecc_odd       <= 11'd0;
            ecc_cnt       <= 9'd0;
        end else begin
            // Synchronize R/B# and detect busy->ready rising edge
            rb_sync0 <= nand_rb_n;
            rb_sync1 <= rb_sync0;
            rb_prev  <= rb_sync1;
            if (rb_sync1 && !rb_prev) begin
                irq_rb_stat <= 1'b1;
            end

            // Register writes
            if (reg_wr) begin
                unique case (reg_addr[7:0])
                    8'h00: begin
                        ce_assert   <= reg_wdata[0];
                        wp_n_reg    <= reg_wdata[1];
                        if (reg_wdata[2]) begin
                            ecc_even <= 11'd0;
                            ecc_odd  <= 11'd0;
                            ecc_cnt  <= 9'd0;
                        end
                        irq_done_en <= reg_wdata[3];
                        irq_rb_en   <= reg_wdata[4];
                    end
                    8'h08: if (!busy) begin
                        cur_op  <= OP_CMD;
                        tx_byte <= reg_wdata[7:0];
                        state   <= ST_SETUP;
                        timer   <= (t_setup == 8'd0) ? 8'd0 : (t_setup - 8'd1);
                    end
                    8'h0C: if (!busy) begin
                        cur_op  <= OP_ADDR;
                        tx_byte <= reg_wdata[7:0];
                        state   <= ST_SETUP;
                        timer   <= (t_setup == 8'd0) ? 8'd0 : (t_setup - 8'd1);
                    end
                    8'h10: if (!busy) begin
                        cur_op  <= OP_WR;
                        tx_byte <= reg_wdata[7:0];
                        state   <= ST_SETUP;
                        timer   <= (t_setup == 8'd0) ? 8'd0 : (t_setup - 8'd1);
                        if (ECC_ENABLE && ecc_cnt < 9'd256) begin
                            logic [21:0] nxt;
                            nxt      = hamming_step(ecc_even, ecc_odd, reg_wdata[7:0], ecc_cnt[7:0]);
                            ecc_even <= nxt[10:0];
                            ecc_odd  <= nxt[21:11];
                            ecc_cnt  <= ecc_cnt + 9'd1;
                        end
                    end
                    8'h14: if (!busy) begin
                        cur_op <= OP_RD;
                        state  <= ST_SETUP;
                        timer  <= (t_setup == 8'd0) ? 8'd0 : (t_setup - 8'd1);
                    end
                    8'h1C: begin
                        t_setup <= (reg_wdata[7:0]   == 8'd0) ? 8'd1 : reg_wdata[7:0];
                        t_pulse <= (reg_wdata[15:8]  == 8'd0) ? 8'd1 : reg_wdata[15:8];
                        t_hold  <= (reg_wdata[23:16] == 8'd0) ? 8'd1 : reg_wdata[23:16];
                    end
                    8'h24: begin
                        if (reg_wdata[0]) irq_done_stat <= 1'b0;
                        if (reg_wdata[1]) irq_rb_stat   <= 1'b0;
                    end
                    default: ;
                endcase
            end

            // Asynchronous ONFI waveform FSM
            unique case (state)
                ST_IDLE: ;
                ST_SETUP: begin
                    if (timer != 8'd0) begin
                        timer <= timer - 8'd1;
                    end else begin
                        state <= ST_PULSE;
                        timer <= (t_pulse == 8'd0) ? 8'd0 : (t_pulse - 8'd1);
                    end
                end
                ST_PULSE: begin
                    if (timer != 8'd0) begin
                        timer <= timer - 8'd1;
                    end else begin
                        if (cur_op == OP_RD) begin
                            rx_byte <= nand_dq_in;
                            if (ECC_ENABLE && ecc_cnt < 9'd256) begin
                                logic [21:0] nxt;
                                nxt      = hamming_step(ecc_even, ecc_odd, nand_dq_in, ecc_cnt[7:0]);
                                ecc_even <= nxt[10:0];
                                ecc_odd  <= nxt[21:11];
                                ecc_cnt  <= ecc_cnt + 9'd1;
                            end
                        end
                        state <= ST_HOLD;
                        timer <= (t_hold == 8'd0) ? 8'd0 : (t_hold - 8'd1);
                    end
                end
                ST_HOLD: begin
                    if (timer != 8'd0) begin
                        timer <= timer - 8'd1;
                    end else begin
                        state         <= ST_IDLE;
                        irq_done_stat <= 1'b1;
                    end
                end
            endcase
        end
    end
endmodule
