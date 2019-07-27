#!/bin/bash
rm strings.s 2>/dev/null
for I in *.txt; do ../tools/string_packer < $I >> strings.s; done
