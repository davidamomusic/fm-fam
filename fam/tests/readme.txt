# FM-FAM: the screenshots of README.md (fam/docs/screens): every page of every mode, and two of the RANDOM pages.
# Each shot is a 240x240 PNG of the screen (build/host/rm_*.png); fam/docs/make_screens.py turns them into the files of the README.
wait 600
tap PLAY
wait 300
# SEQ: three pages (the app starts there, on the first page)
wait 80
expect mode 0
expect page 0
shot rm_seq1.png
spin SELECT 1
wait 80
shot rm_seq2.png
spin SELECT 1
wait 80
expect page 2
shot rm_seq3.png
spin SELECT -2
wait 60
# SYNTH: six pages
tap EDIT
wait 80
expect mode 1
shot rm_syn1.png
spin SELECT 1
wait 80
shot rm_syn2.png
spin SELECT 1
wait 80
shot rm_syn3.png
spin SELECT 1
wait 80
shot rm_syn4.png
spin SELECT 1
wait 80
shot rm_syn5.png
spin SELECT 1
wait 80
expect page 5
shot rm_syn6.png
spin SELECT -6
wait 60
# PATCH: the patch bay
tap LFO
wait 80
expect mode 2
turn PRESET 3
wait 3000
shot rm_patch1.png
turn PRESET -3
wait 3000
# KICK: five pages
tap ARP
wait 80
expect mode 3
shot rm_kick1.png
spin SELECT 1
wait 80
shot rm_kick2.png
spin SELECT 1
wait 80
shot rm_kick3.png
spin SELECT 1
wait 80
shot rm_kick4.png
spin SELECT 1
wait 80
expect page 4
shot rm_kick5.png
spin SELECT -5
wait 60
# FX: seven pages
tap FX
wait 80
expect mode 4
shot rm_fx1.png
spin SELECT 1
wait 80
shot rm_fx2.png
spin SELECT 1
wait 80
shot rm_fx3.png
spin SELECT 1
wait 80
shot rm_fx4.png
spin SELECT 1
wait 80
shot rm_fx5.png
spin SELECT 1
wait 80
shot rm_fx6.png
spin SELECT 1
wait 80
expect page 6
shot rm_fx7.png
spin SELECT -7
wait 60
# PATTERNS: five pages, the last one (BLUETOOTH) with two devices around on the simulator's stand-in
tap GLO
wait 80
expect mode 5
shot rm_patt1.png
spin SELECT 1
wait 80
shot rm_patt2.png
spin SELECT 1
wait 80
shot rm_patt3.png
spin SELECT 1
wait 80
shot rm_patt4.png
spin SELECT 1
wait 80
expect page 4
spin K1 1
expect bton 1
wait 300
bta add HEADSET
bta add SPEAKER
spin K4 1
wait 1300
expect btn 2
shot rm_patt5.png
spin K1 -1
expect bton 0
wait 3000
spin SELECT -4
wait 60
# SCALE: two pages
tap SEL
wait 80
expect mode 7
shot rm_scale1.png
spin SELECT 1
wait 80
shot rm_scale2.png
spin SELECT -2
wait 60
# HOME: projects, the five preset lists and the four help pages
tap SAVE
wait 80
expect mode 8
shot rm_home1.png
spin SELECT 1
wait 80
shot rm_home2.png
spin SELECT 1
wait 80
shot rm_home3.png
spin SELECT 1
wait 80
shot rm_home4.png
spin SELECT 1
wait 80
shot rm_home5.png
spin SELECT 1
wait 80
shot rm_home6.png
spin SELECT 1
wait 80
shot rm_home7.png
spin SELECT 1
wait 80
shot rm_home8.png
spin SELECT 1
wait 80
shot rm_home9.png
spin SELECT 1
wait 80
expect page 9
shot rm_home10.png
spin SELECT -10
wait 60
# RANDOM: the page of the synth's oscillators and the page of the kick sequence
tap EDIT
tap REC
wait 80
expect mode 6
shot rm_rand_syn1.png
tap REC
wait 60
expect mode 1
tap ARP
tap REC
wait 80
expect mode 6
shot rm_rand_kick.png
tap REC
wait 60
expect mode 3
