jitterdebugger

+------------+------------------+
|   Branch   |   Build Status   |
+============+==================+
| ``master`` | |travis-master|_ |
+------------+------------------+
| ``next``   | |travis-next|_   |
+------------+------------------+

.. |travis-master| image:: https://travis-ci.org/igaw/jitterdebugger.svg?branch=master
.. _travis-master: https://travis-ci.org/igaw/jitterdebugger/branches
.. |travis-next| image:: https://travis-ci.org/igaw/jitterdebugger.svg?branch=next
.. _travis-next: https://travis-ci.org/igaw/jitterdebugger/branches

This tool is a reimplementation of cyclictest. It doesn't have all the
command line options as cyclictest which results are easy to get wrong
and therefore an invalid latency report.

The default settings of jitterdebugger will produce a correct
measurrement out of the box.
