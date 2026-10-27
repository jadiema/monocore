//============================================================================
//  MONOCORE-1 for MiSTer: the NVRAM (32 KB, 0x1800_0000) saved to the SD card.
//
//  The OSD entry that loads a ROM is "FS1" (CONF_STR in monocore.sv): at the end of the download
//  the MiSTer mounts saves/MONOCORE/<rom name>.sav on the virtual disk 0 (hps_io img_mounted,
//  before ioctl_download falls; the file may grow). Then, as the MiSTer cores with a backup RAM do
//  (NES_MiSTer):
//    - the file holds data: it is read into the NVRAM, 64 blocks of 512 bytes, the console held
//      in reset meanwhile (hold), so the game finds its save when it starts;
//    - the file is empty (a new game): the NVRAM is cleared, so the game does not find the save of
//      the previous one (the NVRAM outlives the loading of a ROM);
//    - it is written when the OSD opens if the console wrote to its NVRAM since (autosave), or
//      from the OSD ("Ecrire la sauvegarde"); "Recharger la sauvegarde" reads it again (the
//      console starts again).
//  A ROM read from the cartridge port has no file: nothing is loaded or saved.
//
//  hps_io in 16-bit mode (WIDE=1), blocks of 512 bytes (BLKSZ=2): sd_buff_addr[7:0] is the 16-bit
//  word in the block; the file is the NVRAM byte for byte (the first byte of a word is its low
//  byte, the NVRAM's byte order).
//============================================================================

module mc_nvsave
(
	input  wire        clk,
	// hps_io
	input  wire        downloading,     // ioctl_download (the ROM)
	input  wire        img_mounted,     // the .sav mounted (end of the download)
	input  wire        img_readonly,
	input  wire [63:0] img_size,
	output reg  [31:0] sd_lba = 0,
	output reg         sd_rd = 0,
	output reg         sd_wr = 0,
	input  wire        sd_ack,
	input  wire [12:0] sd_buff_addr,
	input  wire [15:0] sd_buff_dout,
	output wire [15:0] sd_buff_din,
	input  wire        sd_buff_wr,
	// OSD
	input  wire        osd_open,        // OSD_STATUS
	input  wire        autosave,        // write when the OSD opens, if the NVRAM changed
	input  wire        req_load,        // read the save again
	input  wire        req_save,        // write it now
	// the NVRAM's second port (rtl/mc_core.v)
	output wire        nv_en,
	output wire [3:0]  nv_we,
	output wire [12:0] nv_addr,
	output wire [31:0] nv_wdata,
	input  wire [31:0] nv_rdata,
	input  wire        nv_written,      // the console wrote to its NVRAM
	// status
	output wire        hold,            // keep the console in reset (loading or clearing)
	output reg         enabled = 0,     // a writable save file is mounted
	output wire        busy
);
	localparam [5:0] LAST_BLOCK = 6'd63;    // 32 KB = 64 blocks of 512 bytes

	localparam [1:0] S_IDLE = 2'd0, S_LOAD = 2'd1, S_SAVE = 2'd2, S_CLEAR = 2'd3;
	reg  [1:0] state = S_IDLE;

	reg        has_data = 0;                // the file mounted is not empty
	reg        pending = 0;                 // the ROM just loaded: load or clear the NVRAM
	reg        dirty = 0;                   // the console wrote to the NVRAM since the last copy
	reg        old_dl = 0, old_ack = 0, old_osd = 0, old_load = 0, old_save = 0;
	reg [12:0] clr_addr = 0;

	// the 16-bit word of the NVRAM the SD buffer is at; its 32-bit word and half
	wire [13:0] widx = {sd_lba[5:0], sd_buff_addr[7:0]};
	reg         half_r = 0;                 // (the RAM answers one cycle later)
	always @(posedge clk) half_r <= widx[0];
	assign sd_buff_din = half_r ? nv_rdata[31:16] : nv_rdata[15:0];

	wire clearing = state == S_CLEAR;
	wire load_wr  = state == S_LOAD && sd_ack && sd_buff_wr;
	assign nv_en    = 1'b1;
	assign nv_addr  = clearing ? clr_addr : widx[13:1];
	assign nv_we    = clearing ? 4'b1111 : load_wr ? (widx[0] ? 4'b1100 : 4'b0011) : 4'b0000;
	assign nv_wdata = clearing ? 32'd0 : {sd_buff_dout, sd_buff_dout};

	assign hold = pending || state == S_LOAD || state == S_CLEAR;
	assign busy = state != S_IDLE;

	always @(posedge clk) begin
		old_dl   <= downloading;
		old_ack  <= sd_ack;
		old_osd  <= osd_open;
		old_load <= req_load;
		old_save <= req_save;

		if (nv_written) dirty <= 1'b1;
		if (~old_ack & sd_ack) {sd_rd, sd_wr} <= 2'b00;   // the request taken by hps_io

		// a ROM loading: the save file comes at its end
		if (~old_dl & downloading) begin enabled <= 1'b0; has_data <= 1'b0; end
		if (downloading && img_mounted) begin enabled <= ~img_readonly; has_data <= |img_size; end
		if (old_dl & ~downloading) pending <= 1'b1;

		if (downloading) state <= S_IDLE;
		else case (state)
			S_IDLE: begin
				if (pending) begin                       // the ROM loaded: its save, or a clean NVRAM
					pending <= 1'b0;
					dirty   <= 1'b0;
					if (enabled && has_data) begin state <= S_LOAD; sd_lba <= 32'd0; sd_rd <= 1'b1; end
					else if (enabled) begin state <= S_CLEAR; clr_addr <= 13'd0; end
				end
				else if (enabled && ~old_load & req_load) begin
					state <= S_LOAD; sd_lba <= 32'd0; sd_rd <= 1'b1; dirty <= 1'b0;
				end
				else if (enabled && ((~old_save & req_save) || (autosave && ~old_osd & osd_open && dirty))) begin
					state <= S_SAVE; sd_lba <= 32'd0; sd_wr <= 1'b1; dirty <= 1'b0;
				end
			end
			S_CLEAR: begin
				clr_addr <= clr_addr + 13'd1;
				if (&clr_addr) state <= S_IDLE;
			end
			default: begin                               // S_LOAD, S_SAVE: block by block
				if (old_ack & ~sd_ack) begin
					if (sd_lba[5:0] == LAST_BLOCK) state <= S_IDLE;
					else begin
						sd_lba <= sd_lba + 32'd1;
						sd_rd  <= state == S_LOAD;
						sd_wr  <= state == S_SAVE;
					end
				end
			end
		endcase
	end
endmodule
