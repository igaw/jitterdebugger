.. SPDX-License-Identifier: MIT

==============
jitterdebugger
==============

jitterdebugger measures wake up latencies. jitterdebugger starts a
thread on each CPU which programs a timer and measures the time it
takes from the timer expiring until the thread which set the timer
runs again.

This tool is a re-implementation of cyclictest. It doesn't have all the
command line options as cyclictest which results are easy to get wrong
and therefore an invalid latency report.

The default settings of jitterdebugger will produce a correct
measurement out of the box.

Furthermore, the tool supports storing all samples for post
processing.

#################
Runtime Depenency
#################

jitterdebugger has only dependency to glibc (incl pthread).

- glibc >= 2.24

jitterplot and jittersamples have additional dependency to

- Python3
- Matlibplot
- Pandas
- HDF5 >= 1.8.17

#####
Usage
#####

When running jitterdebugger without any command line options, there
wont be any output until terminated by sending CTRL-C. It will print
out the wake up latency for each CPU including a histogram of all
latencies as JSON. The output can direclty be saved into a file using
the -f command line option.

::

  # jitterdebugger
  ^C
  {
    "cpu": {
      "0": {
        "histogram": {
          "3": 3678,
          "4": 220,
          "5": 1,
          "8": 1,
          "11": 1
        },
        "count": 3901,
        "min": 3,
        "max": 11,
        "avg": 3.06
      },
      "1": {
        "histogram": {
          "3": 3690,
          "4": 188,
          "5": 2,
          "8": 3,
          "9": 2,
          "18": 1
        },
        "count": 3886,
        "min": 3,
        "max": 18,
        "avg": 3.06
      }
    }
  }

When providing '-v', jitterdebugger will live update all counters:

::

  # jitterdebugger  -v
  affinity: 0,1 = 2 [0x3]
  T: 0 (  614) A: 0 C:     13476 Min:         3 Avg:    3.08 Max:        10
  T: 1 (  615) A: 1 C:     13513 Min:         3 Avg:    3.10 Max:        20
  ^C
  {
    "cpu": {
      "0": {
        "histogram": {
          "3": 4070,
          "4": 269,
          "5": 26,
          "6": 5,
          "7": 1,
          "8": 1,
          "9": 2,
          "10": 1
        },
        "count": 4375,
        "min": 3,
        "max": 10,
        "avg": 3.08
      },
      "1": {
        "histogram": {
          "3": 4002,
          "4": 320,
          "5": 22,
          "6": 4,
          "7": 2,
          "8": 1,
          "10": 2,
          "11": 1,
          "16": 2,
          "20": 1
        },
        "count": 4357,
        "min": 3,
        "max": 20,
        "avg": 3.10
      }
    }
  }


Field explanation:

- T:   Thread id (PID)
- A:   CPU affinity
- C:   Number of measurement cycles
- Min: Smallest wake up latency observed
- Max: Biggest wake up latency observed
- Avg: Arithmetic average of all observed wake up latencies.


################
Measurement loop
################

The tool will start a measurement thread on each available CPU.

The measurement loop does following:

::

  next = clock_gettime(CLOCK_MONOTONIC) + 1000us
  while not terminated:
    next = next + 1000us

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, next)

    now = clock_gettime()
    diff = now - next

    store(diff)


##############
Histogram plot
##############

This project provides a very simple analisys tool to a
histogram. First let jitterdebugger collect some data and store the
output into a file.

::

  # jitterdebugger -f results.json
  ^C
  # jitterplot hist results.json


#################
Exporting samples
#################

jitterdebugger is able to store all samples to a binary file. For post
processing use jittersamples to print data as normal ASCII output:

::

  # jitterdebugger -o samples.raw
  ^C
  # jittersamples samples.raw | head
  0;1114.936950838;9
  0;1114.937204763;3
  0;1114.937458457;3
  0;1114.937711970;3
  0;1114.937965595;3
  0;1114.938218986;3
  0;1114.938472416;3
  0;1114.938725788;3
  0;1114.938979191;3
  0;1114.939232594;3

The fields are:

1. CPUID
2. Timestamp in seconds
3. Wake up latency in micro seconds
