# sidecurl

Building cURL and sidecurl:

```
export SIDECAR_HOME=$HOME/sidecar
$SIDECAR_HOME/deps/build_deps.sh 3  # libcurl
$SIDECAR_HOME/deps/build_deps.sh 4  # sidecurl
```

## Supported Options

See `man curl` for more info; behavior of these options is intended to be
compatible with `curl`.

```
-o, --output <file>              write to <file> instead of stdout
-1, --http1.1                    tell curl to use HTTP v1.1
-3, --http3                      tell curl to use HTTP v3
    --sidecar <threshold>        enable the sidecar and set the power sum quACK threshold
    --mark-acked <bool>          use quacks to consider packets received (default: 0)
    --mark-lost-and-retx <bool>  use quacks to consider packets lost, and retransmit (default: 1)
    --update-cwnd <bool>         use quacks to update the cwnd on loss (default: 1)
    --near-delay <ms>            set the estimated delay between the sender and proxy, in ms (default: 1)
    --e2e-delay <ms>             set the estimated delay between the proxy and receiver, in ms (default: 26)
    --enable-reset <bool>        whether to send sidecar reset messages (default: 1)
    --reset-port <port>          port to send sidecar reset messages to (default: 1234)
    --reset-threshold <ms>       threshold that determines frequency of sidecar reset messages (default: 10)
    --reorder-threshold <pkts>   threshold for sidecar loss detection (default: 3)
-u, --quack-style <style>        style of quack to send/receive
    --disable-mtu-fix            disable fix that sends packets only if the cwnd > mtu
-M, --min-ack-delay <ms>         minimum delay between acks, in ms
-D, --max-ack-delay <ms>         maximum delay between acks, in ms
    --header header              extra header to include in information sent
-w, --write-out <format>         format string for display on stdout afterwards
-d, --data-binary @<file>        send the contents of @<file> as an HTTP POST
                                 NOTE: only @<file> supported currently, not <str>
-k, --insecure                   tell curl not to attempt to validate peer signatures
-m, --max-time <secs>            timeout the operation after this many seconds
```

## Testing

_WARNING: The following documentation is outdated._

Build the sidecurl clients with debug output using `make clean` and `make debug`.

### (udp)sidecurl

Send quacks repeatedly:

```
$ watch -n1 "echo \"quack.quack\" | nc -q 0 -u -N 127.0.0.1 5103"
```

In a separate shell, run `sidecurl`, e.g., on a long download:

```
$ ./sidecurl https://old-releases.ubuntu.com/releases/22.04.1/ubuntu-22.04.1-desktop-amd64.iso -t 1 -o /dev/null
New quack: 'quack.quack' (recieved 0 maincar bytes)
New quack: 'quack.quack' (recieved 311296 maincar bytes)
New quack: 'quack.quack' (recieved 13238272 maincar bytes)
New quack: 'quack.quack' (recieved 32866304 maincar bytes)
...
```

Note that sidecurl will do a POST if you pass `--data-binary @<file>`.

### tcpsidecurl

Run `tcpsidecurl` on a long download:

```
$ ./tcpsidecurl https://old-releases.ubuntu.com/releases/22.04.1/ubuntu-22.04.1-desktop-amd64.iso -t 1 -o /dev/null
```

In another shell:

```
$ nc 127.0.0.1 5103
type type type
type type type
type type type
```

Caveats with current implementation:

* `tcpsidecurl` won't start doing anything (including the underlying curl
  work) until it makes a TCP connection on the sidecar socket.
* If the sidecar socket disconnects, `tcpsidecurl` will spin trying to read
  from it.
