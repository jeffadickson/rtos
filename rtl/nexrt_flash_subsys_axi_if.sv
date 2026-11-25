// SPDX-License-Identifier: BSD-2-Clause
// SystemVerilog interface-port wrapper around nexrt_flash_subsys_axi using
// nexrt_axi_lite_if.slave for clean SoC interconnect hookup.
`timescale 1ns/1ps

module nexrt_flash_subsys_axi_if #(
    parameter logic [31:0] BASE_ADDR        = 32'h1002_0000,
    parameter bit          ENABLE_SPI       = 1'b1,
    parameter int unsigned SPI_NUM_CS       = 2,
    parameter bit          ENABLE_ONFI_NAND = 1'b1,
    parameter bit          NAND_HW_ECC      = 1'b1,
    parameter bit          ENABLE_CFI_NOR   = 1'b1,
    parameter int unsigned NOR_ADDR_BITS    = 24
) (
    nexrt_axi_lite_if.slave          s_axi,

    output logic                     spi_sclk,
    output logic                     spi_mosi,
    input  logic                     spi_miso,
    output logic [SPI_NUM_CS-1:0]    spi_cs_n,
    output logic                     spi_wp_n,
    output logic                     spi_hold_n,

    output logic                     nand_ce_n,
    output logic                     nand_cle,
    output logic                     nand_ale,
    output logic                     nand_we_n,
    output logic                     nand_re_n,
    output logic                     nand_wp_n,
    output logic [7:0]               nand_dq_out,
    output logic                     nand_dq_oe,
    input  logic [7:0]               nand_dq_in,
    input  logic                     nand_rb_n,

    output logic [NOR_ADDR_BITS-1:0] nor_addr,
    output logic                     nor_ce_n,
    output logic                     nor_oe_n,
    output logic                     nor_we_n,
    output logic                     nor_reset_n,
    output logic                     nor_wp_n,
    output logic [15:0]              nor_dq_out,
    output logic                     nor_dq_oe,
    input  logic [15:0]              nor_dq_in,
    input  logic                     nor_ry_by_n,

    output logic                     flash_irq
);
    nexrt_flash_subsys_axi #(
        .BASE_ADDR        (BASE_ADDR),
        .ENABLE_SPI       (ENABLE_SPI),
        .SPI_NUM_CS       (SPI_NUM_CS),
        .ENABLE_ONFI_NAND (ENABLE_ONFI_NAND),
        .NAND_HW_ECC      (NAND_HW_ECC),
        .ENABLE_CFI_NOR   (ENABLE_CFI_NOR),
        .NOR_ADDR_BITS    (NOR_ADDR_BITS)
    ) u_subsys (
        .clk           (s_axi.clk),
        .rst_n         (s_axi.rst_n),
        .s_axi_awaddr  (s_axi.awaddr),
        .s_axi_awvalid (s_axi.awvalid),
        .s_axi_awready (s_axi.awready),
        .s_axi_wdata   (s_axi.wdata),
        .s_axi_wstrb   (s_axi.wstrb),
        .s_axi_wvalid  (s_axi.wvalid),
        .s_axi_wready  (s_axi.wready),
        .s_axi_bresp   (s_axi.bresp),
        .s_axi_bvalid  (s_axi.bvalid),
        .s_axi_bready  (s_axi.bready),
        .s_axi_araddr  (s_axi.araddr),
        .s_axi_arvalid (s_axi.arvalid),
        .s_axi_arready (s_axi.arready),
        .s_axi_rdata   (s_axi.rdata),
        .s_axi_rresp   (s_axi.rresp),
        .s_axi_rvalid  (s_axi.rvalid),
        .s_axi_rready  (s_axi.rready),
        .spi_sclk      (spi_sclk),
        .spi_mosi      (spi_mosi),
        .spi_miso      (spi_miso),
        .spi_cs_n      (spi_cs_n),
        .spi_wp_n      (spi_wp_n),
        .spi_hold_n    (spi_hold_n),
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
        .flash_irq     (flash_irq)
    );
endmodule
