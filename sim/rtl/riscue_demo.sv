`timescale 1ns/1ns
// Simulation integration only: actual unchanged upstream PLAIN pipeline + AXI
// bridge, 32 KiB ordinary RAM, and a TX-only UARTLite-offset peripheral.
// No ECC/radiation or board timing claim is made for this wrapper.
module riscue_demo;
 logic clk=0,reset_n=0;
 always #5 clk=~clk;
 initial begin repeat(8) @(negedge clk); reset_n=1; end
 wire [31:0] awaddr,wdata,araddr,rdata;
 wire [3:0] wstrb;
 wire awvalid,awready,wvalid,wready,bvalid,bready,arvalid,arready,rvalid,rready;
 wire [1:0] bresp,rresp;
 wire fatal,halted,trap,uart_tx;
 wire [31:0] pc,trap_cause;
 logic [31:0] timer_traps=0,ecall_traps=0,bad_traps=0;
 always @(posedge clk) if(reset_n && trap) begin
  if(trap_cause==32'h80000007) timer_traps<=timer_traps+1;
  else if(trap_cause==11) ecall_traps<=ecall_traps+1;
  else bad_traps<=bad_traps+1;
 end
 // PLAIN uses raw words in the low 32 bits, whereas the upstream AXI bridge
 // always consumes/produces SECDED codewords. Adapt BOTH directions explicitly.
 // Connecting riscue_axi_core directly in PLAIN mode would decode instruction
 // data incorrectly. This boundary conversion does not protect ordinary RAM.
 import ft_pkg::*;
 wire request,ready,bus_error,core_fatal,bridge_fatal;
 wire [38:0] raw_address,raw_write,raw_control,coded_read;
 ecc_result_t decoded_read;
 assign decoded_read=ecc_decode(coded_read);
 assign fatal=core_fatal|bridge_fatal;
 riscue_pipeline_core #(.PIPELINE_PROTECTION(2),.RF_TMR(0),.FI_ENABLE(0),.BUS_TIMEOUT(4096)) cpu(
  .clk(clk),.rst_n(reset_n),.external_irq(1'b0),
  .external_fault(bridge_fatal|(ready && decoded_read.uncorrectable)),
  .halt_request(1'b0),.boot_pc(32'b0),
  .debug_haltreq(1'b0),.debug_resumereq(1'b0),.debug_reg_valid(1'b0),
  .debug_reg_write(1'b0),.debug_regno(16'b0),.debug_wdata(32'b0),
  .req(request),.ready(ready),.bus_error(bus_error),
  .addr_code(raw_address),.wdata_code(raw_write),.ctrl_code(raw_control),
  .rdata_code({7'b0,decoded_read.data}),
  .fatal(core_fatal),.halted(halted),.trap(trap),.pc(pc),.trap_cause(trap_cause),
  .fi_valid(1'b0),.fi_target(4'b0),.fi_index(32'b0),.fi_mask(512'b0));
 riscue_axi_master_bridge bridge(
  .clk(clk),.rst_n(reset_n),.core_req(request),
  .addr_code(ecc_encode(raw_address[31:0])),
  .wdata_code(ecc_encode(raw_write[31:0])),
  .ctrl_code(ecc_encode(raw_control[31:0])),
  .rdata_code(coded_read),.core_ready(ready),.core_error(bus_error),.fatal(bridge_fatal),
  .m_axi_awaddr(awaddr),.m_axi_awvalid(awvalid),.m_axi_awready(awready),
  .m_axi_wdata(wdata),.m_axi_wstrb(wstrb),.m_axi_wvalid(wvalid),.m_axi_wready(wready),
  .m_axi_bresp(bresp),.m_axi_bvalid(bvalid),.m_axi_bready(bready),
  .m_axi_araddr(araddr),.m_axi_arvalid(arvalid),.m_axi_arready(arready),
  .m_axi_rdata(rdata),.m_axi_rresp(rresp),.m_axi_rvalid(rvalid),.m_axi_rready(rready));
 nexrt_demo_axi_bus bus(.*);
endmodule

// Independent AW/W latches: AXI address and data need not arrive together.
// Responses remain asserted until accepted; byte strobes apply to RAM writes.
module nexrt_demo_axi_bus(
 input wire clk,reset_n,
 input wire [31:0] awaddr,wdata,araddr,
 input wire [3:0] wstrb,
 input wire awvalid,wvalid,bready,arvalid,rready,
 output wire awready,wready,arready,
 output logic bvalid,rvalid,
 output logic [1:0] bresp,rresp,
 output logic [31:0] rdata,
 output wire uart_tx
);
 logic [31:0] ram[0:8191];
 string firmware;
 initial begin
  if(!$value$plusargs("firmware=%s",firmware)) $fatal(1,"Missing +firmware=hex image");
  $readmemh(firmware,ram);
 end
 logic have_aw,have_w;
 logic [31:0] saved_aw,saved_w;
 logic [3:0] saved_strobe;
 logic uart_start;
 logic [7:0] uart_data;
 wire uart_ready;
 // Accelerated 8-clock 8N1 bits for simulation; not the board baud setting.
 riscue_uart_tx #(.CLOCK_HZ(100000000),.BAUD(12500000)) transmitter(
  .clk(clk),.rst_n(reset_n),.valid(uart_start),.data(uart_data),
  .ready(uart_ready),.tx(uart_tx));
 assign awready=reset_n && !have_aw && !bvalid;
 assign wready=reset_n && !have_w && !bvalid;
 assign arready=reset_n && !rvalid;
 always @(posedge clk) begin
  if(!reset_n) begin
   have_aw<=0;have_w<=0;bvalid<=0;rvalid<=0;
   bresp<=0;rresp<=0;rdata<=0;saved_aw<=0;saved_w<=0;saved_strobe<=0;
   uart_start<=0;uart_data<=0;
  end else begin
   uart_start<=0;
   if(bvalid && bready) bvalid<=0;
   if(rvalid && rready) rvalid<=0;
   if(awvalid && awready) begin saved_aw<=awaddr;have_aw<=1;end
   if(wvalid && wready) begin saved_w<=wdata;saved_strobe<=wstrb;have_w<=1;end
   if(have_aw && have_w && !bvalid) begin
    have_aw<=0;have_w<=0;bvalid<=1;bresp<=0;
    if(saved_aw<32768 && saved_aw[1:0]==0) begin
     for(integer b=0;b<4;b=b+1)
      if(saved_strobe[b]) ram[saved_aw[14:2]][b*8+:8]<=saved_w[b*8+:8];
    end else if(saved_aw==32'h10000004 && saved_strobe[0] && uart_ready) begin
     uart_data<=saved_w[7:0];uart_start<=1;
    end else bresp<=2; // SLVERR: unmapped, misaligned, or TX full.
   end
   if(arvalid && arready) begin
    rvalid<=1;rresp<=0;rdata<=0;
    if(araddr<32768 && araddr[1:0]==0) rdata<=ram[araddr[14:2]];
    else if(araddr==32'h10000008) rdata<=uart_ready?32'h4:32'h8;
    else rresp<=2;
   end
  end
 end
endmodule
