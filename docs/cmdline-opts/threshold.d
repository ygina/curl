c: Copyright (C) 2022, Gina Yuan, <gyuan@cs.stanford.edu>, et al.
SPDX-License-Identifier: curl
Long: threshold
Arg: <num_packets>
Help: Maximum number of lost packets in the quACK.
Category: important curl
Example: --threshold 20
---
Maximum number of lost packets in the quACK. If the threshold is exceeded, the
difference quACK cannot be decoded. Parameter is only used if the sidecar is
enabled with the `--sidecar <interface>` option.
