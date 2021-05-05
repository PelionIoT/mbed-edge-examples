# ----------------------------------------------------------------------------
# Copyright 2018 ARM Ltd.
# ----------------------------------------------------------------------------
# Description:
#   Renders stress_tester_clip.h from stress_tester.docopt.
#

python3 ../lib/docopt.c/docopt_c.py -t ../cli_template.tmpl -o stress_tester_clip.h stress_tester.docopt

