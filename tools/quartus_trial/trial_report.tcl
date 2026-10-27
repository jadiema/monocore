# Writes trial_fmax_slow.txt, trial_fmax_fast.txt (maximum frequency), trial_paths.txt (the 40 slowest
# paths in detail), trial_paths_summary.txt (the 200 slowest) and trial_blocks.txt (the worst path ending
# in each block of the design, so that every block can be judged at once), after a full compile.
# Works from the Quartus GUI (Tools > Tcl Scripts > Run) or from a command prompt:
#   quartus_sta -t trial_report.tcl
set here [file dirname [file normalize [info script]]]
cd $here
if {![string match -nocase "quartus_sta*" [file tail [info nameofexecutable]]]} {
  # timing commands only exist in quartus_sta: run this same script with it
  set sta [file join [file dirname [info nameofexecutable]] quartus_sta]
  puts "Analyse de timing en cours (1 a 3 minutes)..."
  if {[catch {exec $sta -t [file join $here trial_report.tcl]} msg]} { puts $msg }
  puts "Rapports ecrits dans $here : trial_fmax_slow.txt, trial_paths.txt, trial_paths_summary.txt, trial_blocks.txt"
  return
}
# blocks (the patterns overlap: "rendu" includes sprites and layers, "core" is the whole design)
set blocks {
  cpu_frontend  {*:cpu|*:frontend|*}
  cpu_decode    {*:cpu|*:decode_u|*}
  cpu_csr       {*:cpu|*:csr_u|*}
  cpu_regs      {*:cpu|*:regs|*}
  cpu_muldiv    {*:cpu|*mul*}
  cpu_reste     {*:cpu|*}
  icache        {*:icache|*}
  dcache        {*:dcache|*}
  rom_arb       {*:rom_arb|*}
  wram          {*:wram|*}
  sprites       {*:render|*:spr|*}
  couches       {*:render|mc_layer:*}
  rendu         {*:render|*}
  gpu           {*:gpu|*}
  vtiming       {*:vtiming|*}
  son           {*:apu|*}
  io            {*:io|*}
  core          {*}
}
proc report_blocks {} {
  global blocks
  set f [open trial_blocks.txt w]
  puts $f [format "%-14s %9s %9s  %s" bloc "marge ns" "Fmax MHz" "pire chemin (depart -> arrivee)"]
  foreach {name pat} $blocks {
    if {[catch {get_keepers $pat} col]} { puts $f [format "%-14s  (erreur : %s)" $name $col]; continue }
    if {[get_collection_size $col] == 0} { puts $f [format "%-14s  (aucun registre)" $name]; continue }
    set paths [get_timing_paths -setup -npaths 1 -to $col]
    if {[get_collection_size $paths] == 0} { puts $f [format "%-14s  (aucun chemin)" $name]; continue }
    foreach_in_collection p $paths {
      set slack [get_path_info $p -slack]
      set fmax [expr {1000.0 / (20.0 - $slack)}]
      set from [get_node_info -name [get_path_info $p -from]]
      set to [get_node_info -name [get_path_info $p -to]]
      puts $f [format "%-14s %9.3f %9.1f  %s -> %s" $name $slack $fmax $from $to]
    }
  }
  close $f
  # details: the 10 worst paths into each block
  file delete trial_blocks_detail.txt
  foreach {name pat} $blocks {
    if {[catch {get_keepers $pat} col]} continue
    if {[get_collection_size $col] == 0} continue
    catch {report_timing -setup -npaths 10 -detail full_path -to $col -panel_name $name -file trial_blocks_detail.txt -append}
  }
}
project_open monocore_trial
foreach model {slow fast} {
  create_timing_netlist -model $model
  read_sdc
  update_timing_netlist
  report_clock_fmax_summary -file trial_fmax_$model.txt
  if {$model == "slow"} {
    report_timing -setup -npaths 40 -detail full_path -file trial_paths.txt
    report_timing -setup -npaths 200 -detail summary -file trial_paths_summary.txt
    report_blocks
  }
  delete_timing_netlist
}
project_close
