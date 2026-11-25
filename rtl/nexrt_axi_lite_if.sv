// SPDX-License-Identifier: BSD-2-Clause
// Reusable SystemVerilog AXI4-Lite interface with master and slave modports
// for NexRT storage and peripheral controllers (RISCue / Zynq / MicroBlaze).
`timescale 1ns/1ps

interface nexrt_axi_lite_if #(
    parameter int unsigned ADDR_W = 32,
    parameter int unsigned DATA_W = 32
) (
    input logic clk,
    input logic rst_n
);
    logic [ADDR_W-1:0]     awaddr;
    logic                  awvalid;
    logic                  awready;

    logic [DATA_W-1:0]     wdata;
    logic [(DATA_W/8)-1:0] wstrb;
    logic                  wvalid;
    logic                  wready;

    logic [1:0]            bresp;
    logic                  bvalid;
    logic                  bready;

    logic [ADDR_W-1:0]     araddr;
    logic                  arvalid;
    logic                  arready;

    logic [DATA_W-1:0]     rdata;
    logic [1:0]            rresp;
    logic                  rvalid;
    logic                  rready;

    modport master (
        input  clk, rst_n,
        output awaddr, awvalid, wdata, wstrb, wvalid, bready,
               araddr, arvalid, rready,
        input  awready, wready, bresp, bvalid,
               arready, rdata, rresp, rvalid
    );

    modport slave (
        input  clk, rst_n,
        input  awaddr, awvalid, wdata, wstrb, wvalid, bready,
               araddr, arvalid, rready,
        output awready, wready, bresp, bvalid,
               arready, rdata, rresp, rvalid
    );
endinterface
