c: Copyright (C) 2022, Gina Yuan, <gyuan@cs.stanford.edu>, et al.
SPDX-License-Identifier: curl
Long: sidecar
Arg: <interface>
Help: Enable the sidecar on this interface.
Category: important curl
Example: --sidecar h2-eth0 $URL
---
Enable the sidecar on this interface. All packets sent on this interface will
be inserted into a quACK. Additionally opens a UDP socket on port 53535 to
listen for quACKs. Decodes the received quACKs against its sent packets to
adjust the retransmission or congestion control mechanisms, for example.
