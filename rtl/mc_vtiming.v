// MONOCORE-1 frame timing: line and frame counters, VBLANK and LINE interrupts.
// A line lasts LINE_CYCLES CPU cycles, a multiple of 4 (pixel clock = 50 MHz / 4) chosen so that
// a frame of SCREEN_H + 24 lines lasts as close to 1/60 s as possible (60.01 Hz at 240 lines).
// The visible height is latched at the start of each frame.
// Interlaced mode (DISP_CTRL bit 10, latched at frame start): SCREEN_H is the full picture height
// (384..512, 0 means 480) and a frame is a field of SCREEN_H / 2 lines, timed like a progressive frame
// of that height; field line v shows picture line 2v + field. A width above 384 (up to 640) is only
// allowed with 270 field lines or fewer.
module mc_vtiming (
	input  wire        clk,
	input  wire        rst_n,
	input  wire [31:0] screen_h_reg,   // SCREEN_H register: 0 means 240, else clamped to 192..288
	input  wire [31:0] screen_w_reg,   // SCREEN_W register: 0 means 320, else clamped to 224..384 (640)
	input  wire        ilace_reg,      // DISP_CTRL bit 10: interlaced
	input  wire [31:0] line_cmp,
	output reg  [9:0]  vcount,
	output reg  [9:0]  height,         // latched visible lines (of the field when interlaced) for the current frame
	output reg  [9:0]  width,          // latched visible width for the current frame
	output reg         interlace,      // latched interlaced mode for the current frame
	output reg         field,          // field parity of the current frame (0 when progressive)
	output wire [9:0]  new_line,       // line that starts this cycle (valid with line_start)
	output wire [9:0]  new_pline,      // picture line rendered in that line period (2 new_line + field when interlaced)
	output wire [9:0]  new_height,
	output wire [9:0]  new_width,
	output reg  [31:0] frame,
	output reg         in_vblank,
	output wire        line_start,     // first cycle of a line
	output wire        vblank_pulse,
	output wire        line_pulse,
	output reg  [15:0] lcount          // cycle within the line
);
	// requested size, clamped, kept in registers (two cycles behind the CPU registers; used at frame start):
	// first the comparisons, then the choice
	reg  [9:0] h_clamped;
	reg  [9:0] w_clamped;
	reg        il_clamped;
	reg        h0, hlo, hhi, ilo, ihi, w0, wlo, whi, wxhi, il;
	reg  [9:0] hv;
	reg  [9:0] wv;
	// field lines: progressive 192..288, interlaced SCREEN_H / 2 in 192..256 (0 gives 240 in both)
	wire [9:0] h_req = h0 ? 10'd240 : il ? (ilo ? 10'd192 : ihi ? 10'd256 : {1'b0, hv[9:1]})
	                                     : (hlo ? 10'd192 : hhi ? 10'd288 : hv);
	wire       wide_ok = h_req <= 10'd270;
	wire [9:0] w_req = w0 ? 10'd320 : wlo ? 10'd224 : wxhi ? (wide_ok ? 10'd640 : 10'd384) :
	                   (whi && !wide_ok) ? 10'd384 : wv;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			h0 <= 1'b1; hlo <= 1'b0; hhi <= 1'b0; ilo <= 1'b0; ihi <= 1'b0; hv <= 10'd0; il <= 1'b0;
			w0 <= 1'b1; wlo <= 1'b0; whi <= 1'b0; wxhi <= 1'b0; wv <= 10'd0;
		end else begin
			h0 <= screen_h_reg == 0; hlo <= screen_h_reg < 32'd192; hhi <= screen_h_reg > 32'd288; hv <= screen_h_reg[9:0];
			ilo <= screen_h_reg < 32'd384; ihi <= screen_h_reg > 32'd512; il <= ilace_reg;
			w0 <= screen_w_reg == 0; wlo <= screen_w_reg < 32'd224; whi <= screen_w_reg > 32'd384;
			wxhi <= screen_w_reg > 32'd640; wv <= screen_w_reg[9:0];
		end
	end
	reg [15:0] line_cycles_m2;          // LINE_CYCLES - 2 for the latched height
	always @(*) begin
		case (height)
      10'd192: line_cycles_m2 = 16'd3854;
      10'd193: line_cycles_m2 = 16'd3838;
      10'd194: line_cycles_m2 = 16'd3818;
      10'd195: line_cycles_m2 = 16'd3802;
      10'd196: line_cycles_m2 = 16'd3782;
      10'd197: line_cycles_m2 = 16'd3766;
      10'd198: line_cycles_m2 = 16'd3750;
      10'd199: line_cycles_m2 = 16'd3734;
      10'd200: line_cycles_m2 = 16'd3718;
      10'd201: line_cycles_m2 = 16'd3698;
      10'd202: line_cycles_m2 = 16'd3682;
      10'd203: line_cycles_m2 = 16'd3666;
      10'd204: line_cycles_m2 = 16'd3650;
      10'd205: line_cycles_m2 = 16'd3634;
      10'd206: line_cycles_m2 = 16'd3618;
      10'd207: line_cycles_m2 = 16'd3602;
      10'd208: line_cycles_m2 = 16'd3586;
      10'd209: line_cycles_m2 = 16'd3574;
      10'd210: line_cycles_m2 = 16'd3558;
      10'd211: line_cycles_m2 = 16'd3542;
      10'd212: line_cycles_m2 = 16'd3526;
      10'd213: line_cycles_m2 = 16'd3514;
      10'd214: line_cycles_m2 = 16'd3498;
      10'd215: line_cycles_m2 = 16'd3482;
      10'd216: line_cycles_m2 = 16'd3470;
      10'd217: line_cycles_m2 = 16'd3454;
      10'd218: line_cycles_m2 = 16'd3438;
      10'd219: line_cycles_m2 = 16'd3426;
      10'd220: line_cycles_m2 = 16'd3410;
      10'd221: line_cycles_m2 = 16'd3398;
      10'd222: line_cycles_m2 = 16'd3382;
      10'd223: line_cycles_m2 = 16'd3370;
      10'd224: line_cycles_m2 = 16'd3358;
      10'd225: line_cycles_m2 = 16'd3342;
      10'd226: line_cycles_m2 = 16'd3330;
      10'd227: line_cycles_m2 = 16'd3318;
      10'd228: line_cycles_m2 = 16'd3302;
      10'd229: line_cycles_m2 = 16'd3290;
      10'd230: line_cycles_m2 = 16'd3278;
      10'd231: line_cycles_m2 = 16'd3262;
      10'd232: line_cycles_m2 = 16'd3250;
      10'd233: line_cycles_m2 = 16'd3238;
      10'd234: line_cycles_m2 = 16'd3226;
      10'd235: line_cycles_m2 = 16'd3214;
      10'd236: line_cycles_m2 = 16'd3202;
      10'd237: line_cycles_m2 = 16'd3190;
      10'd238: line_cycles_m2 = 16'd3178;
      10'd239: line_cycles_m2 = 16'd3166;
      10'd240: line_cycles_m2 = 16'd3154;
      10'd241: line_cycles_m2 = 16'd3142;
      10'd242: line_cycles_m2 = 16'd3130;
      10'd243: line_cycles_m2 = 16'd3118;
      10'd244: line_cycles_m2 = 16'd3106;
      10'd245: line_cycles_m2 = 16'd3094;
      10'd246: line_cycles_m2 = 16'd3082;
      10'd247: line_cycles_m2 = 16'd3070;
      10'd248: line_cycles_m2 = 16'd3058;
      10'd249: line_cycles_m2 = 16'd3050;
      10'd250: line_cycles_m2 = 16'd3038;
      10'd251: line_cycles_m2 = 16'd3026;
      10'd252: line_cycles_m2 = 16'd3014;
      10'd253: line_cycles_m2 = 16'd3006;
      10'd254: line_cycles_m2 = 16'd2994;
      10'd255: line_cycles_m2 = 16'd2982;
      10'd256: line_cycles_m2 = 16'd2974;
      10'd257: line_cycles_m2 = 16'd2962;
      10'd258: line_cycles_m2 = 16'd2950;
      10'd259: line_cycles_m2 = 16'd2942;
      10'd260: line_cycles_m2 = 16'd2930;
      10'd261: line_cycles_m2 = 16'd2918;
      10'd262: line_cycles_m2 = 16'd2910;
      10'd263: line_cycles_m2 = 16'd2898;
      10'd264: line_cycles_m2 = 16'd2890;
      10'd265: line_cycles_m2 = 16'd2878;
      10'd266: line_cycles_m2 = 16'd2870;
      10'd267: line_cycles_m2 = 16'd2858;
      10'd268: line_cycles_m2 = 16'd2850;
      10'd269: line_cycles_m2 = 16'd2842;
      10'd270: line_cycles_m2 = 16'd2830;
      10'd271: line_cycles_m2 = 16'd2822;
      10'd272: line_cycles_m2 = 16'd2810;
      10'd273: line_cycles_m2 = 16'd2802;
      10'd274: line_cycles_m2 = 16'd2794;
      10'd275: line_cycles_m2 = 16'd2782;
      10'd276: line_cycles_m2 = 16'd2774;
      10'd277: line_cycles_m2 = 16'd2766;
      10'd278: line_cycles_m2 = 16'd2754;
      10'd279: line_cycles_m2 = 16'd2746;
      10'd280: line_cycles_m2 = 16'd2738;
      10'd281: line_cycles_m2 = 16'd2730;
      10'd282: line_cycles_m2 = 16'd2718;
      10'd283: line_cycles_m2 = 16'd2710;
      10'd284: line_cycles_m2 = 16'd2702;
      10'd285: line_cycles_m2 = 16'd2694;
      10'd286: line_cycles_m2 = 16'd2686;
      10'd287: line_cycles_m2 = 16'd2674;
      10'd288: line_cycles_m2 = 16'd2666;
		default: line_cycles_m2 = 16'd3154;
		endcase
	end

	// the end of the line and the next line number are prepared in registers: they only change at the
	// start of a line, and are next needed thousands of cycles later
	reg started;
	reg        last_q;              // lcount == line_cycles - 1
	reg [15:0] lcm2;                // line_cycles - 2
	reg [9:0]  next_line;
	reg        next_zero;
	wire last_cycle = last_q;
	assign line_start = !started || last_cycle;
	assign new_line = !started ? 10'd0 : next_line;
	wire   nl_zero  = !started || next_zero;
	assign new_height = nl_zero ? h_clamped : height;
	assign new_width = nl_zero ? w_clamped : width;
	wire   new_ilace = nl_zero ? il_clamped : interlace;
	wire   new_field = nl_zero ? (il_clamped && interlace && !field) : field;
	assign new_pline = new_ilace ? {new_line[8:0], new_field} : new_line;
	assign vblank_pulse = line_start && new_line == new_height;
	assign line_pulse = line_start && {22'd0, new_line} == line_cmp;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			last_q <= 1'b0; lcm2 <= 16'd3154; next_line <= 10'd1; next_zero <= 1'b0;
			h_clamped <= 10'd240; w_clamped <= 10'd320; il_clamped <= 1'b0;
		end else begin
			h_clamped <= h_req; w_clamped <= w_req; il_clamped <= il;
			lcm2 <= line_cycles_m2;
			last_q <= started && !line_start && lcount == lcm2;
			next_line <= (vcount == height + 10'd23) ? 10'd0 : vcount + 10'd1;
			next_zero <= vcount == height + 10'd23;
		end
	end

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			started <= 1'b0; vcount <= 10'd0; height <= 10'd240; width <= 10'd320; frame <= 32'd0;
			interlace <= 1'b0; field <= 1'b0;
			in_vblank <= 1'b0; lcount <= 16'd0;
		end else begin
			started <= 1'b1;
			if (line_start) begin
				lcount <= 16'd0;
				vcount <= new_line;
				height <= new_height;
				width <= new_width;
				interlace <= new_ilace;
				field <= new_field;
				if (new_line == 10'd0) in_vblank <= 1'b0;
				if (vblank_pulse) begin in_vblank <= 1'b1; frame <= frame + 32'd1; end
			end else lcount <= lcount + 16'd1;
		end
	end
endmodule
