// test top: mc_nvsave + the NVRAM (mc_ram32_tdp), port A as the CPU
module nvtop (
	input  wire clk,
	input  wire downloading, img_mounted, img_readonly, input wire [63:0] img_size,
	output wire [31:0] sd_lba, output wire sd_rd, sd_wr, input wire sd_ack,
	input  wire [12:0] sd_buff_addr, input wire [15:0] sd_buff_dout, output wire [15:0] sd_buff_din, input wire sd_buff_wr,
	input  wire osd_open, autosave, req_load, req_save,
	input  wire cpu_en, input wire [3:0] cpu_we, input wire [12:0] cpu_addr, input wire [31:0] cpu_wdata, output wire [31:0] cpu_rdata,
	output wire hold, enabled, busy
);
	wire nv_en; wire [3:0] nv_we; wire [12:0] nv_addr; wire [31:0] nv_wdata, nv_rdata;
	mc_nvsave s (.clk(clk), .downloading(downloading), .img_mounted(img_mounted), .img_readonly(img_readonly), .img_size(img_size),
		.sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack), .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout),
		.sd_buff_din(sd_buff_din), .sd_buff_wr(sd_buff_wr), .osd_open(osd_open), .autosave(autosave), .req_load(req_load), .req_save(req_save),
		.nv_en(nv_en), .nv_we(nv_we), .nv_addr(nv_addr), .nv_wdata(nv_wdata), .nv_rdata(nv_rdata),
		.nv_written(cpu_en && cpu_we != 0), .hold(hold), .enabled(enabled), .busy(busy));
	mc_ram32_tdp #(.AW(13)) ram (.clk(clk), .a_en(cpu_en), .a_we(cpu_we), .a_addr(cpu_addr), .a_wdata(cpu_wdata), .a_rdata(cpu_rdata),
		.b_en(nv_en), .b_we(nv_we), .b_addr(nv_addr), .b_wdata(nv_wdata), .b_rdata(nv_rdata));
endmodule
