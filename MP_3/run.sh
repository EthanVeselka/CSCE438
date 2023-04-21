#!/bin/bash
CHB=$1;
SHB=$2;

echo "Initializing TinySNS..."
./coordinator -h $CHB &
sleep 1
./server -p 9020 -t master -id 0 &
./server -p 9021 -t slave -id 0 &
./synchronizer -p 9001 -id 0 -h $SHB &
./server -p 9022 -t master -id 1 &
./server -p 9023 -t slave -id 1 &
./synchronizer -p 9002 -id 1 -h $SHB &
./server -p 9024 -t master -id 2 &
./server -p 9025 -t slave -id 2 &
./synchronizer -p 9003 -id 2 -h $SHB &

echo "To kill all processes, run: sh kill.sh"
echo "To clear database, run: sh clean.sh"
