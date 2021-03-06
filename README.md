# pingpong

[![Build Status](https://travis-ci.org/grisu48/pingpong.svg?branch=master)](https://travis-ci.org/grisu48/pingpong)

Stupid simple udp echo server for latency measurements

## Compile

`make` will compile the new `bw` tool

* `bw` - New unified test (latency and bandwidth)

`make legacy` will compile the following:

* `echod` - Echo server
* `udp_ping` - Simple udp ping program
* `tcp_ping` - Simple tcp ping program
* `latency` - Stupid simple latency test program
* `throughput` - Simple throughput test program

## Unified tests using `bw`

`bw` (shorthand for bandwidth) unified bandwith and latency tests using very low-level standard TCP/IP sockets.

    ./bw -s                    # Server end
    ./bw REMOTE                # Client end
    
    ./bw --warmup N  REMOTE    # Client run, but run a warmup for N seconds

## Legacy tests


### echod - Echo daemon

`echod` is a simple echo daemon, that listens on a specific port (default: 7) for udp datagrams and tcp connections.
It echos all incoming packets back to the sender.

    ./echod [OPTIONS] [PORT]

If you want to run echod as `daemon`, please consult first the internal help

    ./echod --help

### Latency (legacy)

Latency test runs a gainst a server, that runs `echod`.

    ./latency [OPTIONS] REMOTE [PORT]

`latency` requires `echod` to run with udp and tcp sockets enabled (default settings for `echod`).
Then you normally just need to run `./latency REMOTE` to get the first results.

As of now, `REMOTE` needs to be an IPv4 address (Shame on me!)

### Throughput/Bandwidth (legacy)

Throughput (bandwidth) tests run agains the `echod` server. The usage is analoge to `latency`

    ./throughput [OPTIONS] REMOTE [PORT]
