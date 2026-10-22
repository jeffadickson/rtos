`timescale 1ns/1ns
// Separate feature test: native atomic-capable PLAIN RAM, no AXI atomic emulation.
// Both configurations use the same core/memory wrapper and 100 MHz timer clock.
module riscue_demo #(parameter bit ZAAMO_ENABLED=0);
 import ft_pkg::*;
 logic clk=0,reset_n=0;
 always #5 clk=~clk;
 initial begin repeat(8) @(negedge clk);reset_n=1;end
 wire request,fatal,halted,trap;
 wire [31:0] pc,trap_cause;
 wire [38:0] address,write_data,control;
 logic ready=0,bus_error=0;
 logic [31:0] read_data=0;
 logic [31:0] ram[0:8191];
 logic [31:0] timer_traps=0,bad_traps=0,amo_requests=0;
 logic [31:0] monitor_address=0;
 wire [31:0] monitor_data=(monitor_address<32768&&monitor_address[1:0]==0)
   ?ram[monitor_address[14:2]]:32'hffffffff;
 string firmware;
 initial begin
  if(!$value$plusargs("firmware=%s",firmware))$fatal(1,"Missing firmware image");
  $readmemh(firmware,ram);
 end
 riscue_pipeline_core #(.ENABLE_ZAAMO(ZAAMO_ENABLED),.AMO_CAPABLE(1),.MULTICYCLE(0),
  .PIPELINE_PROTECTION(2),.RF_TMR(0),.FI_ENABLE(0),.BUS_TIMEOUT(4096)) cpu(
  .clk(clk),.rst_n(reset_n),.external_irq(1'b0),.external_fault(1'b0),
  .halt_request(1'b0),.boot_pc(32'b0),.debug_haltreq(1'b0),.debug_resumereq(1'b0),
  .debug_reg_valid(1'b0),.debug_reg_write(1'b0),.debug_regno(16'b0),.debug_wdata(32'b0),
  .req(request),.ready(ready),.bus_error(bus_error),.addr_code(address),
  .wdata_code(write_data),.ctrl_code(control),.rdata_code({7'b0,read_data}),
  .fatal(fatal),.halted(halted),.trap(trap),.pc(pc),.trap_cause(trap_cause),
  .fi_valid(1'b0),.fi_target(4'b0),.fi_index(32'b0),.fi_mask(512'b0));
 always @(posedge clk)begin
  if(!reset_n)begin ready<=0;bus_error<=0;read_data<=0;end
  else begin
   if(trap)begin
    if(trap_cause==32'h80000007)timer_traps<=timer_traps+1;
    else if(trap_cause!=11)bad_traps<=bad_traps+1;
   end
   ready<=0;bus_error<=0;
   if(request&&!ready)begin
    ready<=1;
    if(address[31:0]>=32768||address[1:0]!=0||
       (control[5]?(!ZAAMO_ENABLED||!amo_control_valid(control[31:0])):control[31:5]!=0))begin
     bus_error<=1;read_data<=0;
    end else begin
     read_data<=ram[address[14:2]];
     if(control[5])begin
      ram[address[14:2]]<=amo_result(control[10:6],ram[address[14:2]],write_data[31:0]);
      amo_requests<=amo_requests+1;
     end else if(control[4])
      for(integer byte_index=0;byte_index<4;byte_index=byte_index+1)
       if(control[byte_index])ram[address[14:2]][byte_index*8+:8]<=write_data[byte_index*8+:8];
    end
   end
  end
 end
endmodule
