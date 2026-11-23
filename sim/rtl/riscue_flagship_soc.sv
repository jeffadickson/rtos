`timescale 1ns/1ns
// Flagship RISCue + NexRT-HWSched + Fault-Injection SoC integration wrapper.
// Connects unchanged upstream riscue_pipeline_core (with RF_TMR=1 and FI_ENABLE=1)
// and riscue_axi_master_bridge to RAM, UARTLite TX, and nexrt_hw_sched_axi at
// 0x1001_0000. Hardware scheduler preempt/deadline/IRQ events drive external_irq.
module riscue_flagship_soc #(
    parameter bit TMR_ENABLE = 1'b1,
    parameter bit FI_ENABLE  = 1'b1
) (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [3:0]  periph_irq,
    input  logic        sched_fi_valid,
    input  logic        sched_fi_double,
    input  logic [3:0]  sched_fi_slot,
    input  logic [15:0] sched_fi_mask,
    input  logic        core_fi_valid,
    input  logic [3:0]  core_fi_target,
    input  logic [31:0] core_fi_index,
    input  logic [511:0] core_fi_mask,
    output logic        uart_tx,
    output logic        fatal,
    output logic        sched_seu_corrected,
    output logic        sched_seu_fault,
    output logic [3:0]  active_tid,
    output logic        active_valid
);
    import ft_pkg::*;

    wire [31:0] awaddr, wdata, araddr;
    wire [3:0]  wstrb;
    wire        awvalid, wvalid, bready, arvalid, rready;
    wire        request, ready, bus_error, core_fatal, bridge_fatal;
    wire [38:0] raw_address, raw_write, raw_control, coded_read;
    wire        halted, trap;
    wire [31:0] pc, trap_cause;
    wire        sched_irq;
    wire [3:0]  next_tid;
    wire        next_valid;

    // Decode between RAM/UART bus (0x0000_0000..0x1000_FFFF) and NexRT-HWSched (0x1001_0000)
    wire sel_sched_wr = (awaddr[31:10] == 22'h040040);
    wire sel_sched_rd = (araddr[31:10] == 22'h040040);

    wire        bus_awready, bus_wready, bus_arready, bus_bvalid, bus_rvalid;
    wire [1:0]  bus_bresp, bus_rresp;
    wire [31:0] bus_rdata;

    wire        sch_awready, sch_wready, sch_arready, sch_bvalid, sch_rvalid;
    wire [1:0]  sch_bresp, sch_rresp;
    wire [31:0] sch_rdata;

    wire        awready = sel_sched_wr ? sch_awready : bus_awready;
    wire        wready  = sel_sched_wr ? sch_wready  : bus_wready;
    wire        arready = sel_sched_rd ? sch_arready : bus_arready;
    wire        bvalid  = sch_bvalid | bus_bvalid;
    wire [1:0]  bresp   = sch_bvalid ? sch_bresp : bus_bresp;
    wire        rvalid  = sch_rvalid | bus_rvalid;
    wire [1:0]  rresp   = sch_rvalid ? sch_rresp : bus_rresp;
    wire [31:0] rdata   = sch_rvalid ? sch_rdata : bus_rdata;

    ecc_result_t decoded_read;
    assign decoded_read = ecc_decode(coded_read);
    assign fatal = core_fatal | bridge_fatal | sched_seu_fault;

    riscue_pipeline_core #(
        .PIPELINE_PROTECTION (2),
        .RF_TMR              (TMR_ENABLE ? 1 : 0),
        .FI_ENABLE           (FI_ENABLE  ? 1 : 0),
        .BUS_TIMEOUT         (4096)
    ) cpu (
        .clk             (clk),
        .rst_n           (rst_n),
        .external_irq    (sched_irq),
        .external_fault  (bridge_fatal | (ready && decoded_read.uncorrectable)),
        .halt_request    (1'b0),
        .boot_pc         (32'b0),
        .debug_haltreq   (1'b0),
        .debug_resumereq (1'b0),
        .debug_reg_valid (1'b0),
        .debug_reg_write (1'b0),
        .debug_regno     (16'b0),
        .debug_wdata     (32'b0),
        .req             (request),
        .ready           (ready),
        .bus_error       (bus_error),
        .addr_code       (raw_address),
        .wdata_code      (raw_write),
        .ctrl_code       (raw_control),
        .rdata_code      ({7'b0, decoded_read.data}),
        .fatal           (core_fatal),
        .halted          (halted),
        .trap            (trap),
        .pc              (pc),
        .trap_cause      (trap_cause),
        .fi_valid        (core_fi_valid),
        .fi_target       (core_fi_target),
        .fi_index        (core_fi_index),
        .fi_mask         (core_fi_mask)
    );

    riscue_axi_master_bridge bridge (
        .clk           (clk),
        .rst_n         (rst_n),
        .core_req      (request),
        .addr_code     (ecc_encode(raw_address[31:0])),
        .wdata_code    (ecc_encode(raw_write[31:0])),
        .ctrl_code     (ecc_encode(raw_control[31:0])),
        .rdata_code    (coded_read),
        .core_ready    (ready),
        .core_error    (bus_error),
        .fatal         (bridge_fatal),
        .m_axi_awaddr  (awaddr),
        .m_axi_awvalid (awvalid),
        .m_axi_awready (awready),
        .m_axi_wdata   (wdata),
        .m_axi_wstrb   (wstrb),
        .m_axi_wvalid  (wvalid),
        .m_axi_wready  (wready),
        .m_axi_bresp   (bresp),
        .m_axi_bvalid  (bvalid),
        .m_axi_bready  (bready),
        .m_axi_araddr  (araddr),
        .m_axi_arvalid (arvalid),
        .m_axi_arready (arready),
        .m_axi_rdata   (rdata),
        .m_axi_rresp   (rresp),
        .m_axi_rvalid  (rvalid),
        .m_axi_rready  (rready)
    );

    nexrt_hw_sched_axi #(
        .MAX_THREADS    (16),
        .MAX_IRQ_ROUTES (4),
        .TMR_ENABLE     (TMR_ENABLE),
        .BASE_ADDR      (32'h1001_0000)
    ) u_hw_sched (
        .clk                 (clk),
        .rst_n               (rst_n),
        .s_axi_awaddr        (awaddr),
        .s_axi_awvalid       (awvalid && sel_sched_wr),
        .s_axi_awready       (sch_awready),
        .s_axi_wdata         (wdata),
        .s_axi_wstrb         (wstrb),
        .s_axi_wvalid        (wvalid && sel_sched_wr),
        .s_axi_wready        (sch_wready),
        .s_axi_bresp         (sch_bresp),
        .s_axi_bvalid        (sch_bvalid),
        .s_axi_bready        (bready),
        .s_axi_araddr        (araddr),
        .s_axi_arvalid       (arvalid && sel_sched_rd),
        .s_axi_arready       (sch_arready),
        .s_axi_rdata         (sch_rdata),
        .s_axi_rresp         (sch_rresp),
        .s_axi_rvalid        (sch_rvalid),
        .s_axi_rready        (rready),
        .periph_irq          (periph_irq),
        .sched_irq           (sched_irq),
        .active_tid          (active_tid),
        .active_valid        (active_valid),
        .next_tid_out        (next_tid),
        .next_valid_out      (next_valid),
        .seu_corrected_pulse (sched_seu_corrected),
        .seu_fault_out       (sched_seu_fault),
        .fi_valid            (sched_fi_valid),
        .fi_double           (sched_fi_double),
        .fi_slot             (sched_fi_slot),
        .fi_mask             (sched_fi_mask)
    );

    nexrt_demo_axi_bus u_ram_uart (
        .clk     (clk),
        .reset_n (rst_n),
        .awaddr  (awaddr),
        .wdata   (wdata),
        .araddr  (araddr),
        .wstrb   (wstrb),
        .awvalid (awvalid && !sel_sched_wr),
        .wvalid  (wvalid && !sel_sched_wr),
        .bready  (bready),
        .arvalid (arvalid && !sel_sched_rd),
        .rready  (rready),
        .awready (bus_awready),
        .wready  (bus_wready),
        .arready (bus_arready),
        .bvalid  (bus_bvalid),
        .rvalid  (bus_rvalid),
        .bresp   (bus_bresp),
        .rresp   (bus_rresp),
        .rdata   (bus_rdata),
        .uart_tx (uart_tx)
    );
endmodule
