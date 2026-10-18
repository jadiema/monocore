// MONOCORE-1 cartridge reader: one or two SPI Flash chips (W25Q64, 8 MB each, 16 MB in all) on the
// USER port. The cartridge holds a .mnc file as is: chip 0 has its first 8 MB, chip 1 the rest.
// On a request, the header is read and checked (signature MNC1, size up to 16 MB), then the whole
// ROM is copied to the SDRAM through the same 16-bit download stream as hps_io (mc_system muxes
// the two). The console stays in reset meanwhile, as for a ROM loaded from the SD card.
//
// SPI mode 0. The USER port lines are open drain (the FPGA only pulls them low, the cartridge's
// pull-ups bring them back up), so the clock is slow to rise: half periods of 8, 4 or 2 cycles
// (3.125, 6.25 or 12.5 MHz). The data are read with Fast Read Dual Output (0x3B, two bits per
// clock on DO and DI, the FPGA releasing DI), or Read Data (0x03) on a single line.
module mc_cart #(
	parameter CHIP_BITS = 23          // 8 MB per chip (W25Q64); smaller in the tests
) (
	input  wire        clk,            // 50 MHz
	input  wire        start,          // pulse: read the cartridge
	input  wire [1:0]  speed,          // 0: 3.125 MHz, 1: 6.25 MHz, 2 and 3: 12.5 MHz
	input  wire        single,         // 1: Read Data (0x03) instead of Fast Read Dual Output
	input  wire        hps_active,     // a download from the SD card: it has priority
	// SPI lines (1 = line released; the board pulls it up)
	output reg         sck,
	output reg         mosi,
	input  wire        mosi_in,        // DI read back (second data line in dual mode)
	input  wire        miso_in,        // DO
	output reg         cs0_n,
	output reg         cs1_n,
	// download stream to the ROM loader
	output reg         dl_active,
	output reg         dl_wr,
	output reg  [26:0] dl_addr,
	output reg  [15:0] dl_data,
	input  wire        dl_wait,
	// status
	output wire        busy,
	output reg         error           // last read failed: no signature or bad size
);
	localparam [31:0] MAX_SIZE = 32'd2 << CHIP_BITS;   // two chips

	// inputs: two-stage synchronisers
	reg [1:0] miso_s, mosi_s;
	always @(posedge clk) begin miso_s <= {miso_s[0], miso_in}; mosi_s <= {mosi_s[0], mosi_in}; end

	// ---------------------------------------------------------------- byte engine
	// op: 0 send tx, 1 receive on DO (8 clocks), 2 receive on DO+DI (4 clocks), 3 dummy (8 clocks)
	reg        eng_go, eng_busy;
	reg  [1:0] eng_op;
	reg  [7:0] tx, rx;
	reg  [7:0] tx_byte;                // byte to send, set by the sequencer, loaded by eng_go
	reg  [3:0] nclk, div;
	wire [3:0] half = speed == 2'd0 ? 4'd8 : speed == 2'd1 ? 4'd4 : 4'd2;
	always @(posedge clk) begin
		if (eng_go) begin
			eng_busy <= 1'b1; sck <= 1'b0; div <= half - 4'd1;
			nclk <= eng_op == 2'd2 ? 4'd4 : 4'd8;
			tx <= tx_byte; mosi <= eng_op == 2'd0 ? tx_byte[7] : 1'b1;
		end else if (eng_busy) begin
			if (div != 4'd0) div <= div - 4'd1;
			else begin
				div <= half - 4'd1;
				if (!sck) sck <= 1'b1;                         // rising edge: the Flash samples DI
				else begin                                     // end of the high phase: sample, fall
					sck <= 1'b0;
					if (eng_op == 2'd2) rx <= {rx[5:0], miso_s[1], mosi_s[1]};
					else rx <= {rx[6:0], miso_s[1]};
					if (eng_op == 2'd0) begin tx <= {tx[6:0], 1'b0}; mosi <= tx[6]; end
					if (nclk == 4'd1) begin eng_busy <= 1'b0; mosi <= 1'b1; end
					nclk <= nclk - 4'd1;
				end
			end
		end
	end
	initial begin eng_busy = 0; sck = 0; mosi = 1; div = 0; nclk = 0; tx = 0; rx = 0; end

	// ---------------------------------------------------------------- sequencer
	localparam S_IDLE = 4'd0, S_HCMD = 4'd1, S_HDR = 4'd2, S_CHECK = 4'd3, S_OPEN = 4'd4,
	           S_LO = 4'd5, S_HI = 4'd6, S_EMIT = 4'd7, S_WAIT = 4'd8, S_CLOSE = 4'd9, S_GAP = 4'd10;
	reg  [3:0]  st;
	reg  [2:0]  idx;                   // byte of the command being sent
	reg  [4:0]  hcnt;                  // header byte (1-16 once started)
	reg         rd_on;                 // S_LO: low byte being read
	reg  [31:0] magic, size;
	reg  [31:0] addr, end_addr;        // byte address in the ROM
	reg  [7:0]  lo;
	reg  [7:0]  gap;
	reg         loading;               // header checked: streaming the ROM
	wire        eng_done = !eng_busy && !eng_go;
	assign busy = st != S_IDLE;

	// the command opening a read at addr: opcode, 24-bit address in the chip, dummy byte (dual)
	wire [7:0] opcode = (single || !loading) ? 8'h03 : 8'h3B;
	wire [23:0] caddr = {{(24-CHIP_BITS){1'b0}}, addr[CHIP_BITS-1:0]};
	wire [2:0] ncmd = (single || !loading) ? 3'd4 : 3'd5;
	reg  [7:0] cmd_byte;
	always @* case (idx)
		3'd0: cmd_byte = opcode;
		3'd1: cmd_byte = caddr[23:16];
		3'd2: cmd_byte = caddr[15:8];
		default: cmd_byte = caddr[7:0];
	endcase

	always @(posedge clk) begin
		eng_go <= 1'b0; dl_wr <= 1'b0;
		if (hps_active && st != S_IDLE) begin               // the SD card takes over
			st <= S_IDLE; dl_active <= 1'b0; cs0_n <= 1'b1; cs1_n <= 1'b1; loading <= 1'b0;
		end else case (st)
		S_IDLE: if (start && !hps_active) begin
			error <= 1'b0; loading <= 1'b0; addr <= 32'd0; idx <= 3'd0; hcnt <= 5'd0;
			cs0_n <= 1'b0; st <= S_HCMD;
		end
		S_HCMD, S_OPEN: if (eng_done) begin               // send the command, then read
			if (idx == ncmd) st <= st == S_HCMD ? S_HDR : S_LO;
			else begin
				eng_op <= idx == 3'd4 ? 2'd3 : 2'd0; tx_byte <= cmd_byte; eng_go <= 1'b1;
				idx <= idx + 3'd1;
			end
		end
		S_HDR: if (eng_done) begin                         // first 16 bytes: signature and size
			if (hcnt >= 5'd1 && hcnt <= 5'd4) magic <= {magic[23:0], rx};
			if (hcnt >= 5'd13) size <= {rx, size[31:8]};
			if (hcnt != 5'd16) begin eng_op <= 2'd1; eng_go <= 1'b1; hcnt <= hcnt + 5'd1; end
			else begin cs0_n <= 1'b1; st <= S_CHECK; end
		end
		S_CHECK: begin
			if (magic != 32'h4D4E4331 || size == 32'd0 || size > MAX_SIZE) begin
				error <= 1'b1; st <= S_IDLE;
			end else begin
				end_addr <= (size + 32'd1) & ~32'd1;         // whole 16-bit words
				loading <= 1'b1; dl_active <= 1'b1; addr <= 32'd0; idx <= 3'd0; gap <= 8'd0;
				st <= S_GAP;
			end
		end
		S_GAP: begin                                       // chip select high for a while
			gap <= gap + 8'd1;
			if (gap == 8'd63) begin
				cs0_n <= addr[CHIP_BITS]; cs1_n <= !addr[CHIP_BITS]; idx <= 3'd0; st <= S_OPEN;
			end
		end
		S_LO: if (eng_done) begin                          // low byte, then high byte
			eng_op <= single ? 2'd1 : 2'd2; eng_go <= 1'b1;
			if (rd_on) begin lo <= rx; rd_on <= 1'b0; st <= S_HI; end
			else rd_on <= 1'b1;
		end
		S_HI: if (eng_done) st <= S_EMIT;
		S_EMIT: if (!dl_wait) begin
			dl_wr <= 1'b1; dl_addr <= addr[26:0]; dl_data <= {rx, lo};
			addr <= addr + 32'd2; st <= S_WAIT;
		end
		S_WAIT: begin                                      // let dl_wait rise, then go on
			if (addr >= end_addr) st <= S_CLOSE;
			else if (addr[CHIP_BITS-1:0] == 0) begin   // next chip
				cs0_n <= 1'b1; cs1_n <= 1'b1; gap <= 8'd0; st <= S_GAP;
			end else begin rd_on <= 1'b0; st <= S_LO; end
		end
		S_CLOSE: if (!dl_wait) begin
			cs0_n <= 1'b1; cs1_n <= 1'b1; dl_active <= 1'b0; loading <= 1'b0; st <= S_IDLE;
		end
		default: st <= S_IDLE;
		endcase
	end
	initial begin
		st = S_IDLE; cs0_n = 1; cs1_n = 1; dl_active = 0; dl_wr = 0; dl_addr = 0; dl_data = 0;
		error = 0; loading = 0; eng_go = 0; idx = 0; hcnt = 0; magic = 0; size = 0; addr = 0;
		end_addr = 0; lo = 0; gap = 0; eng_op = 0; rd_on = 0; tx_byte = 0;
	end
endmodule
