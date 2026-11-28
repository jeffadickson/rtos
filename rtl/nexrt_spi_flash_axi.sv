// SPDX-License-Identifier: BSD-2-Clause
// AXI4-Lite SPI Master Flash Controller for NexRT RTOS_STORAGE:
//   - CS0#: Serial SPI NOR  (Winbond W25Q128JV / MX25L12835F / S25FL128L)
//   - CS1#: Serial SPI NAND (Winbond W25N01GV / Micron MT29F1G01ABAFD)
// Supports SPI Mode 0/3, programmable clock divider, manual CS framing,
// full-duplex 8-bit transfers, and transfer-complete interrupt.
`timescale 1ns/1ps

module nexrt_spi_flash_axi #(
    parameter logic [31:0] BASE_ADDR = 32'h1002_0000,
    parameter int unsigned NUM_CS    = 2
) (
    input  logic              clk,
    input  logic              rst_n,

    // AXI4-Lite slave interface
    input  logic [31:0]       s_axi_awaddr,
    input  logic              s_axi_awvalid,
    output logic              s_axi_awready,
    input  logic [31:0]       s_axi_wdata,
    input  logic [3:0]        s_axi_wstrb,
    input  logic              s_axi_wvalid,
    output logic              s_axi_wready,
    output logic [1:0]        s_axi_bresp,
    output logic              s_axi_bvalid,
    input  logic              s_axi_bready,

    input  logic [31:0]       s_axi_araddr,
    input  logic              s_axi_arvalid,
    output logic              s_axi_arready,
    output logic [31:0]       s_axi_rdata,
    output logic [1:0]        s_axi_rresp,
    output logic              s_axi_rvalid,
    input  logic              s_axi_rready,

    // External SPI Flash pins
    output logic              spi_sclk,
    output logic              spi_mosi,
    input  logic              spi_miso,
    output logic [NUM_CS-1:0] spi_cs_n,
    output logic              spi_wp_n,
    output logic              spi_hold_n,
    output logic              irq_done
);
    localparam logic [15:0] SPI_MAGIC = 16'h5350; // "SP"

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

    // Control & configuration registers
    logic        ctrl_en;
    logic        ctrl_cpol;
    logic        ctrl_cpha;
    logic [3:0]  ctrl_cs_sel;
    logic        ctrl_cs_assert;
    logic [15:0] ctrl_clk_div;
    logic        irq_en;
    logic        irq_stat;

    // SPI shifter FSM
    logic        busy;
    logic        rx_valid;
    logic [7:0]  tx_shift;
    logic [7:0]  rx_shift;
    logic [7:0]  rx_data;
    logic [3:0]  bit_idx;
    logic        phase;     // 0 = first half-cycle, 1 = second half-cycle
    logic [15:0] div_cnt;
    logic        sclk_reg;

    assign spi_sclk   = busy ? sclk_reg : ctrl_cpol;
    assign spi_mosi   = tx_shift[7];
    assign spi_wp_n   = 1'b1;
    assign spi_hold_n = 1'b1;
    assign irq_done   = irq_en && irq_stat;

    always_comb begin
        for (int i = 0; i < NUM_CS; ++i) begin
            if (ctrl_en && ctrl_cs_assert && (ctrl_cs_sel == i[3:0]))
                spi_cs_n[i] = 1'b0;
            else
                spi_cs_n[i] = 1'b1;
        end
    end

    always_comb begin
        reg_rdata = 32'd0;
        reg_err   = 1'b0;
        unique case (reg_addr)
            12'h000: reg_rdata = {ctrl_clk_div, 7'd0, ctrl_cs_assert, ctrl_cs_sel, 1'b0, ctrl_cpha, ctrl_cpol, ctrl_en};
            12'h004: reg_rdata = {SPI_MAGIC, 8'd0, NUM_CS[3:0], 1'b0, !busy, rx_valid, busy};
            12'h008: reg_rdata = {24'd0, tx_shift};
            12'h00C: reg_rdata = {24'd0, rx_data};
            12'h010: reg_rdata = {31'd0, irq_en};
            12'h014: reg_rdata = {31'd0, irq_stat};
            default: begin
                reg_rdata = 32'd0;
                reg_err   = (reg_wr || reg_rd);
            end
        endcase
    end

    wire _unused_spi = &{1'b0, reg_wdata[15:9], reg_wdata[3]};

    wire [15:0] eff_div = (ctrl_clk_div == 16'd0) ? 16'd1 : ctrl_clk_div;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ctrl_en        <= 1'b1;
            ctrl_cpol      <= 1'b0;
            ctrl_cpha      <= 1'b0;
            ctrl_cs_sel    <= 4'd0;
            ctrl_cs_assert <= 1'b0;
            ctrl_clk_div   <= 16'd1;
            irq_en         <= 1'b0;
            irq_stat       <= 1'b0;
            busy           <= 1'b0;
            rx_valid       <= 1'b0;
            tx_shift       <= 8'hFF;
            rx_shift       <= 8'd0;
            rx_data        <= 8'd0;
            bit_idx        <= 4'd0;
            phase          <= 1'b0;
            div_cnt        <= 16'd0;
            sclk_reg       <= 1'b0;
        end else begin
            // Clear rx_valid only on valid 12-bit RXDATA read (no aliasing on 0x10C..0xF0C)
            if (reg_rd && !reg_err && reg_addr == 12'h00C) begin
                rx_valid <= 1'b0;
            end

            // Register writes (full 12-bit address decode, gated by !reg_err)
            if (reg_wr && !reg_err) begin
                unique case (reg_addr)
                    12'h000: begin
                        ctrl_en        <= reg_wdata[0];
                        ctrl_cpol      <= reg_wdata[1];
                        ctrl_cpha      <= reg_wdata[2];
                        ctrl_cs_sel    <= reg_wdata[7:4];
                        ctrl_cs_assert <= reg_wdata[8];
                        ctrl_clk_div   <= (reg_wdata[31:16] == 16'd0) ? 16'd1 : reg_wdata[31:16];
                    end
                    12'h008: begin
                        if (ctrl_en && !busy) begin
                            tx_shift <= reg_wdata[7:0];
                            rx_shift <= 8'd0;
                            bit_idx  <= 4'd0;
                            phase    <= 1'b0;
                            div_cnt  <= eff_div - 16'd1;
                            sclk_reg <= ctrl_cpol;
                            busy     <= 1'b1;
                            rx_valid <= 1'b0;
                        end
                    end
                    12'h010: irq_en <= reg_wdata[0];
                    12'h014: if (reg_wdata[0]) irq_stat <= 1'b0;
                    default: ;
                endcase
            end

            // Full-duplex SPI shift state machine (MSB first, full half-cycle setup & hold in Modes 0-3)
            if (busy) begin
                if (div_cnt != 16'd0) begin
                    div_cnt <= div_cnt - 16'd1;
                end else begin
                    div_cnt <= eff_div - 16'd1;
                    if (!phase) begin
                        // Leading edge: toggle SCLK; CPHA=0 samples MISO; CPHA=1 shifts next MOSI bit (bits 1..7)
                        sclk_reg <= ~ctrl_cpol;
                        phase    <= 1'b1;
                        if (!ctrl_cpha) begin
                            rx_shift <= {rx_shift[6:0], spi_miso};
                        end else if (bit_idx != 4'd0) begin
                            tx_shift <= {tx_shift[6:0], 1'b1};
                        end
                    end else begin
                        // Trailing edge: return SCLK to idle; CPHA=1 samples MISO (holding MOSI); CPHA=0 shifts MOSI
                        sclk_reg <= ctrl_cpol;
                        phase    <= 1'b0;
                        if (ctrl_cpha) begin
                            rx_shift <= {rx_shift[6:0], spi_miso};
                        end
                        if (bit_idx == 4'd7) begin
                            busy     <= 1'b0;
                            rx_valid <= 1'b1;
                            rx_data  <= ctrl_cpha ? {rx_shift[6:0], spi_miso} : rx_shift;
                            irq_stat <= 1'b1;
                        end else begin
                            if (!ctrl_cpha) begin
                                tx_shift <= {tx_shift[6:0], 1'b1};
                            end
                            bit_idx  <= bit_idx + 4'd1;
                        end
                    end
                end
            end
        end
    end
endmodule
