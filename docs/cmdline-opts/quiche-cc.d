c: Copyright (C) 2022, Gina Yuan, <gyuan@cs.stanford.edu>, et al.
SPDX-License-Identifier: curl
Long: quiche-cc
Arg: <cc_algorithm>
Help: Quiche HTTP/3 congestion control algorithm [reno|cubic|bbr].
Category: important curl
Example: --quiche-cc cubic
---
Quiche HTTP/3 congestion control algorithm. Parameter is only used
with the `--http3` option.
