#!/usr/bin/python
#
# history_test: tests the history command
# 
# Test the history command for proper output
#

import sys, imp, atexit, pexpect, proc_check, signal, time, threading
from testutils import *

console = setup_tests()

# ensure that shell prints expected prompt
expect_prompt()

# run program 
sendline("ls")

# run builtin command
run_builtin('history')

# expect the shell to print
expect_prompt("Shell did not print expected prompt ")

assert  ('1'), "index not properly displayed"
assert  ('ls'), "command not properly displayed"

# run program 
sendline("ls")
# run program 
sendline("ls | grep c")
# run program 
sendline("ls")

run_builtin('history')

# expect the shell to print
expect_prompt("Shell did not print expected prompt ")

assert  ('1'), "index not properly displayed"
assert  ('ls'), "command not properly displayed"
assert  ('2'), "index not properly displayed"
assert  ('ls'), "command not properly displayed"
assert  ('3'), "index not properly displayed"
assert  ('ls | grep c'), "command not properly displayed"
assert  ('4'), "index not properly displayed"
assert  ('ls'), "command not properly displayed"

sendline("sleep 1")

# sleep for long enough to ensure that the sleep program has terminated 
time.sleep(2)

run_builtin('history')

# expect the shell to print
expect_prompt("Shell did not print expected prompt ")

assert  ('5'), "index not properly displayed"
assert  ('sleep'), "command not properly displayed"

#exit
sendline("exit");
expect("exit\r\n", "Shell output extraneous characters")

test_success()
