#!/usr/bin/env bash

mode=$1

dev1=tun0
dev2=tun1

_add_tun() {
    dev=$1
    addr=$2
    
    echo "Creating $dev, addr $addr"

    ip tuntap add mode tun dev $dev
    ip link set dev $dev up
    ip route add $addr/24 dev $dev
    #ip addr add $addr/24 dev $dev

    echo "Route to $addr: $(ip route get $addr)"
}

_remove_tun() {
    dev=$1

    ip link set dev $dev down
    ip tuntap del mode tun dev $dev
}

_setup() {
    _add_tun $dev1 "10.45.1.0"
    _add_tun $dev2 "10.45.2.0"
}

_teardown() {
    _remove_tun $dev1
    _remove_tun $dev2
}

if [ "$mode" = "teardown" ]; then
    _teardown
else
    _setup
fi
