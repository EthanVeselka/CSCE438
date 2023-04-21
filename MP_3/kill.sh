#!/bin/bash

echo "Terminating TinySNS..."
pkill coordinator
pkill synchronizer
pkill server
pkill client
