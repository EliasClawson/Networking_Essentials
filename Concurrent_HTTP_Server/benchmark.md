# Benchmark Results

## Configuration
- Command: `./wrk -t10 -c10 -d30s -R10000 http://127.0.0.1:8084/page.html`

## Results

### Single-threaded
 Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    13.97s     3.91s   20.76s    58.40%
    Req/Sec   292.20     36.54   309.00     90.00%
  85610 requests in 30.00s, 106.22MB read
  Socket errors: connect 0, read 85600, write 0, timeout 9
Requests/sec:   2853.22
Transfer/sec:      3.54MB


### Thread-pool
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    14.17s     3.78s   20.66s    58.45%
    Req/Sec   210.40    139.83   313.00     70.00%
  65032 requests in 30.01s, 80.69MB read
  Socket errors: connect 0, read 65022, write 0, timeout 36
Requests/sec:   2167.33
Transfer/sec:      2.69MB


### Async
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     5.54s   886.46ms   6.47s    84.80%
    Req/Sec   199.56    310.03     0.93k    84.00%
  91895 requests in 30.01s, 114.02MB read
  Socket errors: connect 0, read 91885, write 0, timeout 73
Requests/sec:   3062.59
Transfer/sec:      3.80MB