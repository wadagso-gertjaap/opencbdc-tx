#!/bin/bash
NUM_LOADGENS=$(expr $1 - 1)
LOADGEN_DELAY=$(expr $2 + 0)

printf "Using $1 loadgens and waiting $LOADGEN_DELAY seconds between starting and stopping them\n"

set -e
OUTPUT_BASE_FOLDER="test-output"
OUTPUT_FOLDER="$OUTPUT_BASE_FOLDER/$(date +%s)-$(git describe --always)"
mkdir -p $OUTPUT_FOLDER
printf "$(date +%s%N) 0\n" >> "$OUTPUT_FOLDER/client-count.log"

docker-compose -f docker-compose-atomizer-test.yml down
echo "Building"
docker-compose -f docker-compose-atomizer-test.yml build atomizer0
echo "Generating preseed"
docker-compose -f docker-compose-atomizer-test.yml up -d watchtower0 watchtower1 atomizer0 archiver0
echo "Waiting for atomizer to be available"
scripts/wait-for-it.sh -s -t 60 172.16.1.20:5555 -- echo "Atomizer up"

for (( i=0; i<=$NUM_LOADGENS; i++ ))
do
    echo "Starting load gen $i"
    docker-compose -f docker-compose-atomizer-test.yml up -d "loadgen$i"
    printf "$(date +%s%N) $((i+1))\n" >> "$OUTPUT_FOLDER/client-count.log"
    echo "Load gen $i running, waiting for $LOADGEN_DELAY seconds"
    sleep $LOADGEN_DELAY
done

echo "All load gens running, waiting for another $(expr $LOADGEN_DELAY \* 2) seconds"
sleep "$(expr $LOADGEN_DELAY \* 2)"

set +e #Ignore errors from here 

#for (( i=$NUM_LOADGENS; i>=0; i-- ))
for (( i=0; i<=$NUM_LOADGENS; i++ ))
do
    echo "Stopping load gen $i"
    docker-compose -f docker-compose-atomizer-test.yml stop "loadgen$i"
    printf "$(date +%s%N) $(expr $NUM_LOADGENS - $i - 1)\n" >> "$OUTPUT_FOLDER/client-count.log"
    echo "Copying latency samples for load gen $i"
    docker cp "opencbdc-loadgen$i:/opt/tx-processor/tx_samples_$i.txt" "$OUTPUT_FOLDER/"
    echo "Archiving logs for load gen $i"
    docker logs "opencbdc-loadgen$i" 2>&1 1>"$OUTPUT_FOLDER/loadgen$i.log"
done

echo "Stopping archiver"
docker-compose -f docker-compose-atomizer-test.yml stop archiver0
echo "Copying throughput samples"
docker cp opencbdc-archiver0:/opt/tx-processor/tp_samples.txt "$OUTPUT_FOLDER/"
echo "Copying archiver logs"
docker logs opencbdc-archiver0 2>&1 1>"$OUTPUT_FOLDER/archiver0.log"

echo "Stopping atomizer"
docker-compose -f docker-compose-atomizer-test.yml stop atomizer0
echo "Copying atomizer logs"
docker logs opencbdc-atomizer0 2>&1 1>"$OUTPUT_FOLDER/raft-atomizer0.log"
docker cp opencbdc-atomizer0:/opt/tx-processor/event_sampler_atomizer.bin "$OUTPUT_FOLDER/"
docker cp opencbdc-atomizer0:/opt/tx-processor/event_sampler_atomizer_controller.bin "$OUTPUT_FOLDER/"
docker cp opencbdc-atomizer0:/opt/tx-processor/event_sampler_send_complete_txs.bin "$OUTPUT_FOLDER/"
docker cp opencbdc-atomizer0:/opt/tx-processor/event_sampler_state_machine.bin "$OUTPUT_FOLDER/"
docker cp opencbdc-atomizer0:/opt/tx-processor/event_sampler_tx_notify.bin "$OUTPUT_FOLDER/"

echo "Stopping watchtower0"
docker-compose -f docker-compose-atomizer-test.yml stop watchtower0
echo "Copying watchtower0 logs"
docker logs opencbdc-watchtower0 2>&1 1>"$OUTPUT_FOLDER/watchtower0.log"

echo "Stopping watchtower1"
docker-compose -f docker-compose-atomizer-test.yml stop watchtower1
echo "Copying watchtower1 logs"
docker logs opencbdc-watchtower1 2>&1 1>"$OUTPUT_FOLDER/watchtower1.log"

echo "Terminating system"
docker-compose -f docker-compose-atomizer-test.yml down

echo "Calculating result"
cd "$OUTPUT_BASE_FOLDER"
python3 ../scripts/local-benchmark-result.py
