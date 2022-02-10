# Local Atomizer Performance Benchmarking

## Introduction

In order to analyze the Atomizer's performance more easily and make running this performance analysis more accessible, there is a local benchmarking setup made that will spawn the system in docker containers, and run up to 10 load generators against the Atomizer. Since we're analyzing the Atomizer's performance bottleneck, we will only run a single instance and make the load generator deliver the transactions directly to it. Sentinels and shards are kept out of this system as they can (1) pose bottlenecks on the local test system that fog the actual problem we're trying to find, and (2) in a deployment setup, sentinels and shards can be scaled out. There will be an archiver running to deliver the blocks to, and a watchtower to deliver the errors to - even though we don't process those to keep the total system load managable.

## System requirements

For running this setup, you need a development work station with at least 16 CPU threads and 64GB of RAM.

## Running the benchmark

Running the benchmark is done using a single command, once you have the repository cloned:

```
scripts/local-benchmark.sh <number of load gens> <how long between adding load gens>
```

The maximum loadgens the script supports is 10 - this will yield about 350k TX/s of load - which is sufficient to run into the current limitations of the Atomizer, that peaks around 170k TX/s.

## Results

Each run will produce an output folder under `test-output` with the format `<timestamp>-<git commit>`, and in it will be a throughput time series plot and a latency time series plot for each of the sampled events.

### Events being sampled

There is an [event_sampler](../src/util/event_sampler/event_sampler.hpp) class added that is used to instrument specific events in the code, the latency of the event and how many transactions were processed in the event. The events are written to disk in a separate thread to prevent interference with the code we are measuring.

Specifically, these events are now being instrumented:

* [`atomizer::insert_complete.discarded.expired`](../src/uhs/atomizer/atomizer/atomizer.cpp#L144)
* [`atomizer::insert_complete.discarded.spent`](../src/uhs/atomizer/atomizer/atomizer.cpp#L152)
* [`atomizer::insert_complete.success`](../src/uhs/atomizer/atomizer/atomizer.cpp#L159)
* [`atomizer::make_block`](../src/uhs/atomizer/atomizer/atomizer.cpp#L42)
* [`controller::server_handler.tx_notify`](../src/uhs/atomizer/atomizer/controller.cpp#L117)
* [`atomizer_raft::send_complete_txs`](../src/uhs/atomizer/atomizer/atomizer_raft.cpp#L113)
* [`atomizer_raft::tx_notify`](../src/uhs/atomizer/atomizer/atomizer_raft.cpp#L97)
* [`state_machine::tx_notify`](../src/uhs/atomizer/atomizer/state_machine.cpp#L66)

For each of these events, assuming they have happened at least once, there will be a latency plot like [this](local-benchmark-output/latency.pdf)

Each event is also plotted on the throughput time series. Because we log the amount of transactions processed in an event (for instance: the transactions included in a block in `make_block`), we can plot the amount of transactions that went through a particular event in the [time series plot](local-benchmark-output/throughput.pdf). This gives us a clear view on where for instance the processing in the `server_handler` starts to diverge from the transactions getting included in a block, because they are expiring.

#### Re-running result calculation

If you want to re-run the result calculation only (and not re-run the benchmark) - for instance to filter events from the time-series plot (see [below](#filtering-the-sampled-events-in-the-time-series-plot)), then remove the `results.json` file from the test output folder you want to re-calculate for, go to the `test-output` folder and run the result script again.

```
cd test-output
rm <test-folder>/results.json
python3 ../scripts/local-benchmark-result.py
```

#### Filtering the sampled events in the time series plot

The time series plot can become quite busy when all events are plotted; so there's a way to filter out events from the time series plot by specifying the environment variable FILTER_TIME_SERIES_EVENTS="[idx,idx2,idx3]". Using the indexes in the event enum, you can define which should be included. Regardless of this filter, the load gen count and transaction load produced by them will be included as series regardless.

```
cd test-output
rm <test-folder>/results.json
FILTERED_TIME_SERIES_EVENTS="[2,4,6,8]" python3 ../scripts/local-benchmark-result.py
```

#### Adding a sampled event

If you want to add a new event to the set of sampled events, then add the event type to the enumeration [here](../src/util/event_sampler/event_sampler.hpp#L16), and add the description of the event to the result calculation script [here](../scripts/local-benchmark-result.py#L25). The numeric enum value must match the index in the array.

