// SPDX-License-Identifier: BSD-2-Clause
// Unified Multi-Flash AXI4-Lite Subsystem for NexRT RTOS_STORAGE.
// Routes one 16 KiB AXI4-Lite slave window across all four flash families:
//   - BASE + 0x0000: SPI Master (CS0# = Serial NOR, CS1# = Serial SLC NAND)
//   - BASE + 0x1000: Parallel ONFI 1.0 x8 NAND + HW 22-bit Hamming SECDED ECC
//   - BASE + 0x2000: Parallel 16-bit CFI/JEDEC NOR
//   - BASE + 0x3000: Subsystem ID / Capability & Aggregated IRQ Status register
// Unused controllers synthesize away completely via ENABLE_* parameters.
`timescale 1ns/1ps

module nexrt_flash_subsys_axi #(
    parameter logic [31:0] BASE_ADDR        = 32'h1002_0000,
    parameter bit          ENABLE_SPI       = 1'b1,
    parameter int unsigned SPI_NUM_CS       = 2,
    parameter bit          ENABLE_ONFI_NAND = 1'b1,
    parameter bit          NAND_HW_ECC      = 1'b1,
    parameter bit          ENABLE_CFI_NOR   = 1'b1,
    parameter int unsigned NOR_ADDR_BITS    = 24
) (
    input  logic                    clk,
    input  logic                    rst_n,

    // Single AXI4-Lite slave port (16 KiB window at BASE_ADDR)
    input  logic [31:0]             s_axi_awaddr,
    input  logic                    s_axi_awvalid,
    output logic                    s_axi_awready,
    input  logic [31:0]             s_axi_wdata,
    input  logic [3:0]              s_axi_wstrb,
    input  logic                    s_axi_wvalid,
    output logic                    s_axi_wready,
    output logic [1:0]              s_axi_bresp,
    output logic                    s_axi_bvalid,
    input  logic                    s_axi_bready,

    input  logic [31:0]             s_axi_araddr,
    input  logic                    s_axi_arvalid,
    output logic                    s_axi_arready,
    output logic [31:0]             s_axi_rdata,
    output logic [1:0]              s_axi_rresp,
    output logic                    s_axi_rvalid,
    input  logic                    s_axi_rready,

    // SPI Flash pins (Serial NOR on CS0#, Serial NAND on CS1#)
    output logic                    spi_sclk,
    output logic                    spi_mosi,
    input  logic                    spi_miso,
    output logic [SPI_NUM_CS-1:0]   spi_cs_n,
    output logic                    spi_wp_n,
    output logic                    spi_hold_n,

    // Parallel ONFI x8 NAND pins
    output logic                    nand_ce_n,
    output logic                    nand_cle,
    output logic                    nand_ale,
    output logic                    nand_we_n,
    output logic                    nand_re_n,
    output logic                    nand_wp_n,
    output logic [7:0]              nand_dq_out,
    output logic                    nand_dq_oe,
    input  logic [7:0]              nand_dq_in,
    input  logic                    nand_rb_n,

    // Parallel 16-bit CFI NOR pins
    output logic [NOR_ADDR_BITS-1:0] nor_addr,
    output logic                    nor_ce_n,
    output logic                    nor_oe_n,
    output logic                    nor_we_n,
    output logic                    nor_reset_n,
    output logic                    nor_wp_n,
    output logic [15:0]             nor_dq_out,
    output logic                    nor_dq_oe,
    input  logic [15:0]             nor_dq_in,
    input  logic                    nor_ry_by_n,

    // Combined interrupt output
    output logic                    flash_irq
);
    localparam logic [31:0] SUBSYS_MAGIC = 32'h4E58_4643; // "NXFC"
    localparam logic [31:0] SPI_BASE     = BASE_ADDR + 32'h0000;
    localparam logic [31:0] NAND_BASE    = BASE_ADDR + 32'h1000;
    localparam logic [31:0] NOR_BASE     = BASE_ADDR + 32'h2000;

    // Single-outstanding AXI4-Lite demultiplexer across the 4 sub-windows (4 KiB each)
    logic        wr_active;
    logic [1:0]  wr_sel;
    logic        rd_active;
    logic [1:0]  rd_sel;

    logic        spi_awready,  spi_wready,  spi_bvalid,  spi_arready,  spi_rvalid;
    logic [1:0]  spi_bresp,    spi_rresp;
    logic [31:0] spi_rdata;
    logic        spi_irq;

    logic        nand_awready, nand_wready, nand_bvalid, nand_arready, nand_rvalid;
    logic [1:0]  nand_bresp,   nand_rresp;
    logic [31:0] nand_rdata;
    logic        nand_irq;

    logic        nor_awready,  nor_wready,  nor_bvalid,  nor_arready,  nor_rvalid;
    logic [1:0]  nor_bresp,    nor_rresp;
    logic [31:0] nor_rdata;
    logic        nor_irq;

    // Subsystem info/capabilities register block (slot 3: BASE + 0x3000)
    logic        sys_awready, sys_wready, sys_bvalid, sys_arready, sys_rvalid;
    logic [1:0]  sys_bresp,   sys_rresp;
    logic [31:0] sys_rdata;

    wire aw_in_win = (s_axi_awaddr[31:14] == BASE_ADDR[31:14]);
    wire ar_in_win = (s_axi_araddr[31:14] == BASE_ADDR[31:14]);
    wire [1:0] aw_slot = s_axi_awaddr[13:12];
    wire [1:0] ar_slot = s_axi_araddr[13:12];

    wire [1:0] cur_wslot = wr_active ? wr_sel : aw_slot;

    // Generate optional controllers
    generate
        if (ENABLE_SPI) begin : g_spi
            nexrt_spi_flash_axi #(
                .BASE_ADDR (SPI_BASE),
                .NUM_CS    (SPI_NUM_CS)
            ) u_spi (
                .clk           (clk),
                .rst_n         (rst_n),
                .s_axi_awaddr  (s_axi_awaddr),
                .s_axi_awvalid (s_axi_awvalid && aw_in_win && (cur_wslot == 2'd0)),
                .s_axi_awready (spi_awready),
                .s_axi_wdata   (s_axi_wdata),
                .s_axi_wstrb   (s_axi_wstrb),
                .s_axi_wvalid  (s_axi_wvalid && (cur_wslot == 2'd0) && (wr_active || (s_axi_awvalid && aw_in_win))),
                .s_axi_wready  (spi_wready),
                .s_axi_bresp   (spi_bresp),
                .s_axi_bvalid  (spi_bvalid),
                .s_axi_bready  (s_axi_bready && (cur_wslot == 2'd0)),
                .s_axi_araddr  (s_axi_araddr),
                .s_axi_arvalid (s_axi_arvalid && ar_in_win && !rd_active && (ar_slot == 2'd0)),
                .s_axi_arready (spi_arready),
                .s_axi_rdata   (spi_rdata),
                .s_axi_rresp   (spi_rresp),
                .s_axi_rvalid  (spi_rvalid),
                .s_axi_rready  (s_axi_rready && rd_active && (rd_sel == 2'd0)),
                .spi_sclk      (spi_sclk),
                .spi_mosi      (spi_mosi),
                .spi_miso      (spi_miso),
                .spi_cs_n      (spi_cs_n),
                .spi_wp_n      (spi_wp_n),
                .spi_hold_n    (spi_hold_n),
                .irq_done      (spi_irq)
            );
        end else begin : g_no_spi
            assign spi_awready = 1'b0;
            assign spi_wready  = 1'b0;
            assign spi_bvalid  = 1'b0;
            assign spi_bresp   = 2'b10;
            assign spi_arready = 1'b0;
            assign spi_rvalid  = 1'b0;
            assign spi_rresp   = 2'b10;
            assign spi_rdata   = 32'd0;
            assign spi_sclk    = 1'b0;
            assign spi_mosi    = 1'b0;
            assign spi_cs_n    = {SPI_NUM_CS{1'b1}};
            assign spi_wp_n    = 1'b1;
            assign spi_hold_n  = 1'b1;
            assign spi_irq     = 1'b0;
        end

        if (ENABLE_ONFI_NAND) begin : g_nand
            nexrt_onfi_nand_axi #(
                .BASE_ADDR  (NAND_BASE),
                .ECC_ENABLE (NAND_HW_ECC)
            ) u_nand (
                .clk           (clk),
                .rst_n         (rst_n),
                .s_axi_awaddr  (s_axi_awaddr),
                .s_axi_awvalid (s_axi_awvalid && aw_in_win && (cur_wslot == 2'd1)),
                .s_axi_awready (nand_awready),
                .s_axi_wdata   (s_axi_wdata),
                .s_axi_wstrb   (s_axi_wstrb),
                .s_axi_wvalid  (s_axi_wvalid && (cur_wslot == 2'd1) && (wr_active || (s_axi_awvalid && aw_in_win))),
                .s_axi_wready  (nand_wready),
                .s_axi_bresp   (nand_bresp),
                .s_axi_bvalid  (nand_bvalid),
                .s_axi_bready  (s_axi_bready && (cur_wslot == 2'd1)),
                .s_axi_araddr  (s_axi_araddr),
                .s_axi_arvalid (s_axi_arvalid && ar_in_win && !rd_active && (ar_slot == 2'd1)),
                .s_axi_arready (nand_arready),
                .s_axi_rdata   (nand_rdata),
                .s_axi_rresp   (nand_rresp),
                .s_axi_rvalid  (nand_rvalid),
                .s_axi_rready  (s_axi_rready && rd_active && (rd_sel == 2'd1)),
                .nand_ce_n     (nand_ce_n),
                .nand_cle      (nand_cle),
                .nand_ale      (nand_ale),
                .nand_we_n     (nand_we_n),
                .nand_re_n     (nand_re_n),
                .nand_wp_n     (nand_wp_n),
                .nand_dq_out   (nand_dq_out),
                .nand_dq_oe    (nand_dq_oe),
                .nand_dq_in    (nand_dq_in),
                .nand_rb_n     (nand_rb_n),
                .irq_out       (nand_irq)
            );
        end else begin : g_no_nand
            assign nand_awready = 1'b0;
            assign nand_wready  = 1'b0;
            assign nand_bvalid  = 1'b0;
            assign nand_bresp   = 2'b10;
            assign nand_arready = 1'b0;
            assign nand_rvalid  = 1'b0;
            assign nand_rresp   = 2'b10;
            assign nand_rdata   = 32'd0;
            assign nand_ce_n    = 1'b1;
            assign nand_cle     = 1'b0;
            assign nand_ale     = 1'b0;
            assign nand_we_n    = 1'b1;
            assign nand_re_n    = 1'b1;
            assign nand_wp_n    = 1'b1;
            assign nand_dq_out  = 8'd0;
            assign nand_dq_oe   = 1'b0;
            assign nand_irq     = 1'b0;
        end

        if (ENABLE_CFI_NOR) begin : g_nor
            nexrt_cfi_nor_axi #(
                .BASE_ADDR (NOR_BASE),
                .ADDR_BITS (NOR_ADDR_BITS)
            ) u_nor (
                .clk           (clk),
                .rst_n         (rst_n),
                .s_axi_awaddr  (s_axi_awaddr),
                .s_axi_awvalid (s_axi_awvalid && aw_in_win && (cur_wslot == 2'd2)),
                .s_axi_awready (nor_awready),
                .s_axi_wdata   (s_axi_wdata),
                .s_axi_wstrb   (s_axi_wstrb),
                .s_axi_wvalid  (s_axi_wvalid && (cur_wslot == 2'd2) && (wr_active || (s_axi_awvalid && aw_in_win))),
                .s_axi_wready  (nor_wready),
                .s_axi_bresp   (nor_bresp),
                .s_axi_bvalid  (nor_bvalid),
                .s_axi_bready  (s_axi_bready && (cur_wslot == 2'd2)),
                .s_axi_araddr  (s_axi_araddr),
                .s_axi_arvalid (s_axi_arvalid && ar_in_win && !rd_active && (ar_slot == 2'd2)),
                .s_axi_arready (nor_arready),
                .s_axi_rdata   (nor_rdata),
                .s_axi_rresp   (nor_rresp),
                .s_axi_rvalid  (nor_rvalid),
                .s_axi_rready  (s_axi_rready && rd_active && (rd_sel == 2'd2)),
                .nor_addr      (nor_addr),
                .nor_ce_n      (nor_ce_n),
                .nor_oe_n      (nor_oe_n),
                .nor_we_n      (nor_we_n),
                .nor_reset_n   (nor_reset_n),
                .nor_wp_n      (nor_wp_n),
                .nor_dq_out    (nor_dq_out),
                .nor_dq_oe     (nor_dq_oe),
                .nor_dq_in     (nor_dq_in),
                .nor_ry_by_n   (nor_ry_by_n),
                .irq_done      (nor_irq)
            );
        end else begin : g_no_nor
            assign nor_awready = 1'b0;
            assign nor_wready  = 1'b0;
            assign nor_bvalid  = 1'b0;
            assign nor_bresp   = 2'b10;
            assign nor_arready = 1'b0;
            assign nor_rvalid  = 1'b0;
            assign nor_rresp   = 2'b10;
            assign nor_rdata   = 32'd0;
            assign nor_addr    = '0;
            assign nor_ce_n    = 1'b1;
            assign nor_oe_n    = 1'b1;
            assign nor_we_n    = 1'b1;
            assign nor_reset_n = 1'b1;
            assign nor_wp_n    = 1'b1;
            assign nor_dq_out  = 16'd0;
            assign nor_dq_oe   = 1'b0;
            assign nor_irq     = 1'b0;
        end
    endgenerate

    // Subsystem ID/Capabilities + SLVERR responder (slot 3 or out-of-window / disabled slot)
    logic        sys_reg_wr, sys_reg_rd, sys_reg_err;
    logic [11:0] sys_reg_addr;
    logic [31:0] sys_reg_wdata, sys_reg_rdata;

    wire wr_to_sys = (!aw_in_win) || (aw_slot == 2'd3) ||
                     (aw_slot == 2'd0 && !ENABLE_SPI) ||
                     (aw_slot == 2'd1 && !ENABLE_ONFI_NAND) ||
                     (aw_slot == 2'd2 && !ENABLE_CFI_NOR);
    wire rd_to_sys = (!ar_in_win) || (ar_slot == 2'd3) ||
                     (ar_slot == 2'd0 && !ENABLE_SPI) ||
                     (ar_slot == 2'd1 && !ENABLE_ONFI_NAND) ||
                     (ar_slot == 2'd2 && !ENABLE_CFI_NOR);

    nexrt_axi_lite_slave #(
        .BASE_ADDR (BASE_ADDR + 32'h3000),
        .WIN_BITS  (12)
    ) u_sys_axi (
        .clk           (clk),
        .rst_n         (rst_n),
        .s_axi_awaddr  (s_axi_awaddr),
        .s_axi_awvalid (s_axi_awvalid && (wr_active ? (wr_sel == 2'd3) : wr_to_sys)),
        .s_axi_awready (sys_awready),
        .s_axi_wdata   (s_axi_wdata),
        .s_axi_wstrb   (s_axi_wstrb),
        .s_axi_wvalid  (s_axi_wvalid && (wr_active ? (wr_sel == 2'd3) : (s_axi_awvalid && wr_to_sys))),
        .s_axi_wready  (sys_wready),
        .s_axi_bresp   (sys_bresp),
        .s_axi_bvalid  (sys_bvalid),
        .s_axi_bready  (s_axi_bready && (cur_wslot == 2'd3)),
        .s_axi_araddr  (s_axi_araddr),
        .s_axi_arvalid (s_axi_arvalid && !rd_active && rd_to_sys),
        .s_axi_arready (sys_arready),
        .s_axi_rdata   (sys_rdata),
        .s_axi_rresp   (sys_rresp),
        .s_axi_rvalid  (sys_rvalid),
        .s_axi_rready  (s_axi_rready && rd_active && (rd_sel == 2'd3)),
        .reg_wr        (sys_reg_wr),
        .reg_rd        (sys_reg_rd),
        .reg_addr      (sys_reg_addr),
        .reg_wdata     (sys_reg_wdata),
        .reg_rdata     (sys_reg_rdata),
        .reg_err       (sys_reg_err)
    );

    always_comb begin
        sys_reg_rdata = 32'd0;
        sys_reg_err   = sys_reg_wr; // Subsystem ID registers are read-only
        if (sys_reg_rd) begin
            unique case (sys_reg_addr)
                12'h000: sys_reg_rdata = SUBSYS_MAGIC;
                12'h004: sys_reg_rdata = {16'd1, SPI_NUM_CS[7:0], 4'd0, NAND_HW_ECC, ENABLE_CFI_NOR, ENABLE_ONFI_NAND, ENABLE_SPI};
                12'h008: sys_reg_rdata = {29'd0, nor_irq, nand_irq, spi_irq};
                default: sys_reg_err = 1'b1;
            endcase
        end
    end

    // Suppress unused warning on sys_reg_wdata
    wire _unused_sys_w = &{1'b0, sys_reg_wdata};

    // Output muxing
    always_comb begin
        s_axi_awready = 1'b0;
        s_axi_wready  = 1'b0;
        s_axi_bvalid  = 1'b0;
        s_axi_bresp   = 2'b10;
        unique case (wr_active ? wr_sel : (wr_to_sys ? 2'd3 : aw_slot))
            2'd0: begin s_axi_awready = spi_awready;  s_axi_wready = spi_wready;  s_axi_bvalid = spi_bvalid;  s_axi_bresp = spi_bresp;  end
            2'd1: begin s_axi_awready = nand_awready; s_axi_wready = nand_wready; s_axi_bvalid = nand_bvalid; s_axi_bresp = nand_bresp; end
            2'd2: begin s_axi_awready = nor_awready;  s_axi_wready = nor_wready;  s_axi_bvalid = nor_bvalid;  s_axi_bresp = nor_bresp;  end
            2'd3: begin s_axi_awready = sys_awready;  s_axi_wready = sys_wready;  s_axi_bvalid = sys_bvalid;  s_axi_bresp = sys_bresp;  end
        endcase

        s_axi_arready = 1'b0;
        s_axi_rvalid  = 1'b0;
        s_axi_rresp   = 2'b10;
        s_axi_rdata   = 32'd0;
        unique case (rd_active ? rd_sel : (rd_to_sys ? 2'd3 : ar_slot))
            2'd0: begin s_axi_arready = spi_arready;  s_axi_rvalid = spi_rvalid;  s_axi_rresp = spi_rresp;  s_axi_rdata = spi_rdata;  end
            2'd1: begin s_axi_arready = nand_arready; s_axi_rvalid = nand_rvalid; s_axi_rresp = nand_rresp; s_axi_rdata = nand_rdata; end
            2'd2: begin s_axi_arready = nor_arready;  s_axi_rvalid = nor_rvalid;  s_axi_rresp = nor_rresp;  s_axi_rdata = nor_rdata;  end
            2'd3: begin s_axi_arready = sys_arready;  s_axi_rvalid = sys_rvalid;  s_axi_rresp = sys_rresp;  s_axi_rdata = sys_rdata;  end
        endcase
    end

    assign flash_irq = spi_irq || nand_irq || nor_irq;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_active <= 1'b0;
            wr_sel    <= 2'd0;
            rd_active <= 1'b0;
            rd_sel    <= 2'd0;
        end else begin
            if (!wr_active && s_axi_awvalid && s_axi_awready) begin
                wr_active <= 1'b1;
                wr_sel    <= wr_to_sys ? 2'd3 : aw_slot;
            end else if (wr_active && s_axi_bvalid && s_axi_bready) begin
                wr_active <= 1'b0;
            end

            if (!rd_active && s_axi_arvalid && s_axi_arready) begin
                rd_active <= 1'b1;
                rd_sel    <= rd_to_sys ? 2'd3 : ar_slot;
            end else if (rd_active && s_axi_rvalid && s_axi_rready) begin
                rd_active <= 1'b0;
            end
        end
    end
endmodule
