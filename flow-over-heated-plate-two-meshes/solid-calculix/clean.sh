#!/bin/sh
set -e -u

. ../../tools/cleaning-tools.sh

clean_calculix .
rm -fv all.msh # The mesh is generated by generate_mesh.py in this case
