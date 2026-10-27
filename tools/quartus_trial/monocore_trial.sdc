# Target: 50 MHz system clock (20 ns), the frequency of the DE10-Nano oscillator on this pin. The real
# core will get it from a PLL; for this trial the constraint is put directly on the input pin.
create_clock -name clk -period 20.000 [get_ports clk]
derive_clock_uncertainty
# the trial's pseudo inputs and outputs are not real interfaces
set_false_path -from [get_ports {rst_n_pin sin}]
set_false_path -to [get_ports {sout[*]}]
