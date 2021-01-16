# Client for ndn neighbor agent

Connects to the local agent and queries data.

## Building
Install rust and use cargo, ie "cargo run" or "cargo build".

## Command

Will connect to the default agent socket and supports the following commands:
- piers: list all the piers this agent know about
- status: list the faces for the current agent
- stats #: list the local stats for face #
- pier-status #: list the faces for the remote pier # (from piers list)
- pier-stats #1 #2: list the stats for pier #1, face #2
