// SPDX-License-Identifier: BSD-2-Clause
// 32-bit word-aligned AXI4-Lite slave bridge converting AXI4-Lite transactions
// into clean single-cycle internal register writes/reads with OKAY/SLVERR responses.
`timescale 1ns/1ps

module nexrt_axi_lite_slave #(
    parameter logic [31:0] BASE_ADDR = 32'h1002_0000,
    parameter int unsigned WIN_BITS  = 12
) (
    input  logic                clk,
    input  logic                rst_n,

    input  logic [31:0]         s_axi_awaddr,
    input  logic                s_axi_awvalid,
    output logic                s_axi_awready,
    input  logic [31:0]         s_axi_wdata,
    input  logic [3:0]          s_axi_wstrb,
    input  logic                s_axi_wvalid,
    output logic                s_axi_wready,
    output logic [1:0]          s_axi_bresp,
    output logic                s_axi_bvalid,
    input  logic                s_axi_bready,

    input  logic [31:0]         s_axi_araddr,
    input  logic                s_axi_arvalid,
    output logic                s_axi_arready,
    output logic [31:0]         s_axi_rdata,
    output logic [1:0]          s_axi_rresp,
    output logic                s_axi_rvalid,
    input  logic                s_axi_rready,

    output logic                reg_wr,
    output logic                reg_rd,
    output logic [WIN_BITS-1:0] reg_addr,
    output logic [31:0]         reg_wdata,
    input  logic [31:0]         reg_rdata,
    input  logic                reg_err
);
    logic        have_aw, have_w;
    logic [31:0] latched_aw;
    logic [31:0] latched_w;
    logic [3:0]  latched_strb;

    assign s_axi_awready = rst_n && !have_aw && !s_axi_bvalid;
    assign s_axi_wready  = rst_n && !have_w  && !s_axi_bvalid;
    assign s_axi_arready = rst_n && !s_axi_rvalid;

    wire do_write  = have_aw && have_w && !s_axi_bvalid;
    wire do_read   = s_axi_arvalid && s_axi_arready;
    wire wr_in_win = (latched_aw[31:WIN_BITS] == BASE_ADDR[31:WIN_BITS]) &&
                     (latched_aw[1:0] == 2'b00) && (latched_strb == 4'hF);
    wire rd_in_win = (s_axi_araddr[31:WIN_BITS] == BASE_ADDR[31:WIN_BITS]) &&
                     (s_axi_araddr[1:0] == 2'b00);

    assign reg_wr    = do_write && wr_in_win;
    assign reg_rd    = do_read  && rd_in_win;
    assign reg_addr  = reg_wr ? latched_aw[WIN_BITS-1:0] : s_axi_araddr[WIN_BITS-1:0];
    assign reg_wdata = latched_w;

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
                s_axi_bresp  <= (wr_in_win && !reg_err) ? 2'b00 : 2'b10;
            end
            if (do_read) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp  <= (rd_in_win && !reg_err) ? 2'b00 : 2'b10;
                s_axi_rdata  <= (rd_in_win && !reg_err) ? reg_rdata : 32'd0;
            end
        end
    end
endmodule
