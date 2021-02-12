# Client for ndn neighbor agent

Connects to the local agent and queries data.

## Building
Install rust and use cargo, ie "cargo run" or "cargo build".

## Command

Will connect to the default agent socket and supports the following commands at the prompt:
- piers: list all the piers this agent knows about
- status: list the faces for the current agent
- status #: list the faces for the remote pier # (from piers list)
- face #1 #2: list the stats for pier #1, face #2
- route name: query local and all piers for info on route name and display it

## Arguments

Will accept the follow arguments, execute the command(s) and exit (no prompt):
- --piers: list all the piers this agent knows about
- --status #: list the faces for pier #
- --face #1 #2: list the stats for pier #1, face #2
