//============================================================================
//  MONOCORE-1 for MiSTer: the virtual console core inside the MiSTer framework (Template_MiSTer).
//
//  This file connects rtl/mister/mc_system.v (core, SDRAM controller, ROM loader, scan-out) to the
//  framework: hps_io (OSD menu, ROM loading, joysticks), the PLL (50 MHz system, 100 MHz SDRAM),
//  the SDRAM pins, video and audio.
//
//  ROM loading: any file chosen in the OSD ("Charger une ROM", .MNC) is copied to the SDRAM, then the
//  console starts. A file named boot.rom in the core's games folder (games/MONOCORE) is loaded at
//  start-up.
//============================================================================

module emu
(
	`include "sys/emu_ports.vh"
);

///////// Default values for ports not used in this core /////////

assign ADC_BUS  = 'Z;
assign USER_OUT = '1;
assign {UART_RTS, UART_TXD, UART_DTR} = 0;
assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
assign {DDRAM_CLK, DDRAM_BURSTCNT, DDRAM_ADDR, DDRAM_DIN, DDRAM_BE, DDRAM_RD, DDRAM_WE} = '0;

assign VGA_SL = 0;
assign VGA_F1 = 0;
assign VGA_SCALER  = 0;
assign VGA_DISABLE = 0;
assign HDMI_FREEZE = 0;
assign HDMI_BLACKOUT = 0;
assign HDMI_BOB_DEINT = 0;

assign AUDIO_MIX = 0;
assign LED_POWER = 0;
assign BUTTONS = 0;

//////////////////////////////////////////////////////////////////

wire [1:0] ar = status[122:121];
assign VIDEO_ARX = (!ar) ? 12'd4 : (ar - 1'd1);
assign VIDEO_ARY = (!ar) ? 12'd3 : 12'd0;

`include "build_id.v"
localparam CONF_STR = {
	"MONOCORE;;",
	"-;",
	"F1,MNC,Charger une ROM;",
	"-;",
	"O[122:121],Format d'image,Original,Plein ecran,[ARC1],[ARC2];",
	"O[3],Lecture SDRAM,Normale,Retardee;",
	"-;",
	"J1,Bouton 1,Bouton 2,Bouton 3,Bouton 4,Bouton 5,Bouton 6,Start,Piece;",
	"jn,A,B,X,Y,L,R,Start,Select;",
	"-;",
	"T[0],Reset;",
	"R[0],Reset et fermer le menu;",
	"v,0;",
	"V,v",`BUILD_DATE
};

wire forced_scandoubler;
wire   [1:0] buttons;
wire [127:0] status;
wire  [31:0] joystick_0, joystick_1;
wire         ioctl_download, ioctl_wr, ioctl_wait;
wire  [26:0] ioctl_addr;
wire  [15:0] ioctl_dout;
wire  [15:0] ioctl_index;

hps_io #(.CONF_STR(CONF_STR), .WIDE(1)) hps_io
(
	.clk_sys(clk_sys),
	.HPS_BUS(HPS_BUS),
	.EXT_BUS(),
	.gamma_bus(),

	.forced_scandoubler(forced_scandoubler),
	.buttons(buttons),
	.status(status),
	.status_menumask(16'd0),

	.joystick_0(joystick_0),
	.joystick_1(joystick_1),

	.ioctl_download(ioctl_download),
	.ioctl_index(ioctl_index),
	.ioctl_wr(ioctl_wr),
	.ioctl_addr(ioctl_addr),
	.ioctl_dout(ioctl_dout),
	.ioctl_wait(ioctl_wait)
);

///////////////////////   CLOCKS   ///////////////////////////////

wire clk_sys, clk_sdram;
pll pll
(
	.refclk(CLK_50M),
	.rst(0),
	.outclk_0(clk_sys),      // 50 MHz: the console
	.outclk_1(clk_sdram)     // 100 MHz: the SDRAM controller, edges aligned with clk_sys
);

wire reset = RESET | status[0] | buttons[1];

///////////////////////   CONSOLE   //////////////////////////////

wire        ce_pix, hs, vs, hblank, vblank, running;
wire  [7:0] r, g, b;
wire [15:0] snd_l, snd_r;
wire [15:0] dq_o;
wire        dq_oe;

mc_system system
(
	.clk(clk_sys),
	.clk2(clk_sdram),
	.reset(reset),

	.dl_active(ioctl_download),
	.dl_wr(ioctl_wr),
	.dl_addr(ioctl_addr),
	.dl_data(ioctl_dout),
	.dl_wait(ioctl_wait),

	.sd_rd_late(status[3]),
	.dip_osd(32'd0),

	.joy1(joystick_0[11:0]),
	.joy2(joystick_1[11:0]),
	.in_sys(3'd0),

	.ce_pix(ce_pix),
	.vga_r(r), .vga_g(g), .vga_b(b),
	.vga_hs(hs), .vga_vs(vs),
	.vga_hblank(hblank), .vga_vblank(vblank),
	.rotation(),
	.width(),
	.height(),

	.audio_l(snd_l),
	.audio_r(snd_r),

	.running(running),
	.dbg_valid(),
	.dbg_char(),
	.dbg_vcount(),
	.dbg_window(),

	.sd_a(SDRAM_A),
	.sd_ba(SDRAM_BA),
	.sd_dq_o(dq_o),
	.sd_dq_oe(dq_oe),
	.sd_dq_i(SDRAM_DQ),
	.sd_dqml(SDRAM_DQML),
	.sd_dqmh(SDRAM_DQMH),
	.sd_ncs(SDRAM_nCS),
	.sd_nras(SDRAM_nRAS),
	.sd_ncas(SDRAM_nCAS),
	.sd_nwe(SDRAM_nWE),
	.sd_cke(SDRAM_CKE)
);

assign SDRAM_DQ = dq_oe ? dq_o : 16'hZZZZ;

// SDRAM clock: the 100 MHz clock inverted, through a DDR output register (as in the MiSTer cores)
altddio_out
#(
	.extend_oe_disable("OFF"),
	.intended_device_family("Cyclone V"),
	.invert_output("OFF"),
	.lpm_hint("UNUSED"),
	.lpm_type("altddio_out"),
	.oe_reg("UNREGISTERED"),
	.power_up_high("OFF"),
	.width(1)
)
sdramclk_ddr
(
	.datain_h(1'b0),
	.datain_l(1'b1),
	.outclock(clk_sdram),
	.dataout(SDRAM_CLK),
	.aclr(1'b0),
	.aset(1'b0),
	.oe(1'b1),
	.outclocken(1'b1),
	.sclr(1'b0),
	.sset(1'b0)
);

///////////////////////   VIDEO / AUDIO   //////////////////////////

assign CLK_VIDEO = clk_sys;
assign CE_PIXEL  = ce_pix;
assign VGA_DE = ~(hblank | vblank);
assign VGA_HS = hs;
assign VGA_VS = vs;
assign VGA_R  = r;
assign VGA_G  = g;
assign VGA_B  = b;

assign AUDIO_S = 1;
assign AUDIO_L = snd_l;
assign AUDIO_R = snd_r;

// LED: on while a ROM is loading
assign LED_USER = ioctl_download;
assign LED_DISK = {1'b0, ~running};

endmodule
