# Named Data Networking Neighbor Discovery (NDND)

NOTE: This is a fork of https://github.com/Zhiyi-Zhang/NDND that was heavily
modified with the intent of being an ad-hoc (serverless) neighbor discovery
protocol for NDN.  It does NOT implement the same protocol but it is based on it.

## An Overview

Named Data Networking proposes a fundamental change to the Internet’s
architecture, moving from a point-to-point to a data-centric model. NDN can run
over layer 2 (WiFi, Bluetooth, etc) or over TCP/UDP/IP. When running over IP,
NDN hosts need a way of automatically discovering and establishing connectivity with each other.
This project provides an implementation of an ad hoc neighbor discovery
service.  It uses multicast to a shared route for clients to discover each other
(no server is needed) allowing NDN hosts in the same network to discover each
other and automatically establish NDN connectivity by creating UDP/IP tunnels
among themselves.  Built as a service but should be easy to add to an
application to provide an integrated discovery service.

## Authors
* Zhiyi Zhang: zhiyi@cs.ucla.edu
* Xinyu Ma: bitmxy@gmail.com
* Tianyuan Yu: tianyuan@cs.ucla.com
* Zhaoning Kong: jonnykong@cs.ucla.edu
* Edward Lu: edwardzlu98@gmail.com
* Steven Stanfield: stanfield@scarecrowtech.com (ad-hoc/multicast version)

We also thank Arthi Padmanabhan (artpad@cs.ucla.edu) for her previous work in NDND.

## How Ad-hoc NDND works?

There are only client (piers) in this version.

### AH-Client:
When starts, it registers three prefixes
* `/ahnd` (default).  This is a shared multicast route that all
clients will register and send an initial interest to announce themselves.
* `/<my_name>/nd-info`. This prefix is used for Clients to exchange face/route info.
* `/<my_name>/nd-keepalive`. This pefix is used for clients to periodically (default
5 minutes) send interests to each other and expect an empty response to verify
the other client is still online.

#### Arrival Interest
When starting neighbour discovery service, AH-Client first sends out an Arrival Interest
to notify other clients of its arrival. 
```
Name: /ahnd/arrival/<IP>/<Port>/<prefix_length>/<prefix>/<timestamp>
```
The `<prefix_length>` here refers the length or size of AH-Client's prefix. For example, prefix `/cs/client01` has
`<prefix_length>` 2 and prefix `/client01` has 1. `<IP>` and `<Port>` refers to
the interface AH-Client wants to communicate with.

#### Arrival Response
Each client that receives an arrival interest will add a face with it's ip/port
and a route to it's prefix.  It will then send an interest directly to the client
to provide this information in return.
```
Name: /<prefix>/nd-info/<IP>/<Port>/<prefix_length>/<prefix>/<timestamp>
```
The remote client will then add a face and route for each interest it receives
and send an empty reply as an acknowledgement.

#### Periodic heartbeat
Each client will send a heartbeat interest to all other known piers (currently
five minute interval).  If it does not get a response it will remove that piers
face and route.  This also provides activity to keep the routes up.


### Local NFD:
AH-Client manages the local NFD to create new face(s) and new route(s) to the neighbors.
It uses the NFD Management Protocol (which can be found here
https://redmine.named-data.net/projects/nfd/wiki/Management) in order to do the following things: 

#### 1) Create a face for all URI's it receives from piers by sending a FIB Management control command (a signed interest)

#### 2) Create a route for all URI and prefix pairs it receives from piers by sending a RIB Management control command (a signed interest)

## Try Ad-hoc NDND in 3 Steps

### Prerequisite:
* Compile and Install ndn-cxx and NFD.
* Running NFD.

### Step 1: Clone the codebase
```
git clone https://github.com/sstanfield/NDND.git
cd NDND
```

### Step 2: Compile it using “make”
```
make RELEASE=1
```
Note, leave off the RELEASE=1 for a debug build (which is fine to test).  If you
do this use build/debug in step 3.

### Step 3: Run it
```
build/release/ah-ndn /my/prefix
```



## Future Work  (These are for the original NDND, this ad hoc version may or may not have a future)

* Add support of Signed Interest after the Signed Interest Format is implemented in ndn-cxx.
* Add Persistent Storage Support.
* Better Scalability of ND-Server

### Long Term:
* Integrate NDND into NDN Control Center
