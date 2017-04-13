# TestFunctionVariables.py
#
# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See https://swift.org/LICENSE.txt for license information
# See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
#
# ------------------------------------------------------------------------------
"""
Tests that Enum variables display correctly
"""
import lldb
from lldbsuite.test.lldbtest import *
import lldbsuite.test.decorators as decorators
import lldbsuite.test.lldbutil as lldbutil
import os
import unittest2


class TestFunctionVariables(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @decorators.swiftTest
    def test_function_variables(self):
        """Tests that function type variables display correctly"""
        self.build()
        self.do_test()

    def setUp(self):
        TestBase.setUp(self)
        self.main_source = "main.swift"
        self.main_source_spec = lldb.SBFileSpec(self.main_source)

    def do_test(self):
        """Tests that Enum variables display correctly"""
        exe_name = "a.out"
        exe = os.path.join(os.getcwd(), exe_name)

        # Create the target
        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)

        # Set the breakpoints
        breakpoint = target.BreakpointCreateBySourceRegex(
            '// Set breakpoint here', self.main_source_spec)
        self.assertTrue(breakpoint.GetNumLocations() > 0, VALID_BREAKPOINT)

        # Launch the process, and do not stop at the entry point.
        process = target.LaunchSimple(None, None, os.getcwd())

        self.assertTrue(process, PROCESS_IS_VALID)

        # Frame #0 should be at our breakpoint.
        threads = lldbutil.get_threads_stopped_at_breakpoint(
            process, breakpoint)

        self.assertTrue(len(threads) == 1)
        thread = threads[0]
        frame = thread.frames[0]
        self.assertTrue(frame, "Frame 0 is valid.")
        # Get the function pointer variable from our frame
        func_ptr_value = frame.FindVariable('func_ptr')

        # Grab the function pointer value as an unsigned load address
        func_ptr_addr = func_ptr_value.GetValueAsUnsigned()

        # Resolve the load address into a section + offset address
        # (lldb.SBAddress)
        func_ptr_so_addr = target.ResolveLoadAddress(func_ptr_addr)

        # Get the debug info function for this address
        func_ptr_function = func_ptr_so_addr.GetFunction()

        # Make sure the function pointer correctly resolved to our a.bar
        # function
        self.assertTrue('a.bar() -> ()' == func_ptr_function.name)

if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lldb.SBDebugger.Terminate)
    unittest2.main()
