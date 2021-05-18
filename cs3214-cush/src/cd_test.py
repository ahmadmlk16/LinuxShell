#!/usr/bin/python
#
# cd_test: tests the cd command
# 
# Test the cd command for proper output
#

import sys, imp, atexit, pexpect, proc_check, signal, time, threading
from testutils import *

console = setup_tests()

# ensure that shell prints expected prompt
expect_prompt()

# run builtin commadnd 
sendline("cd")

# expect the shell to print
expect_prompt("Shell did not print expected prompt ")

assert  ('Wrong format'), "error not displayed"
assert  ('cd requires exactly one argument'), "error not displayed"

# run builtin commadnd with switch
sendline("cd -d")

# expect the shell to print
expect_prompt("Shell did not print expected prompt ")

# run builtin commadnd with switch
sendline("cd ~")

# expect the shell to print
expect_prompt("Shell did not print expected prompt ")

# run builtin commadnd with a wrong path
sendline("cd wsfwefwarfg")

# expect the shell to print
expect_prompt("Shell did not print expected prompt ")

assert  ('Path not recognized.'), "error not displayed"

#exit
sendline("exit");
expect("exit\r\n", "Shell output extraneous characters")

test_success()