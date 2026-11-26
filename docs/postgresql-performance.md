# PostgreSQL query performance

[Русская версия](ru/postgresql-performance.md)

To repeat the measurement, run `task postgres:performance`. The command creates a separate database, prepares the original schema without the indexes under test, fills the tables, collects statistics and captures the plans. It then installs the indexes under test and repeats the same operations on the same data. After the check the temporary database is dropped.

## Workload

One site has 20,000 map sources, 20,000 jobs in different states, 500 plans and 20,000 camera placements. The log contains 20,000 alarms. One camera has 10,000 recording blocks and 10,000 partly overlapping gap intervals. The test covers expired intervals, blocks waiting for registration, the selection of a specific binding and a request larger than one timeline page.

## Result of the reference run

The table gives the median of three runs with `EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON)` with the usual planner settings. Buffer accesses are the sum of `Shared Hit Blocks` and `Shared Read Blocks` of the root node. A query can touch the same page several times. The results and the row order before and after must match.

| Operation | Buffer accesses before | After | Time before, ms | After, ms |
| --- | ---: | ---: | ---: | ---: |
| Next pending map source | 331 | 2 | 2.45 | 0.03 |
| Next job ready to run | 10 | 5 | 0.19 | 0.14 |
| Jobs that refer to a source | 364 | 3 | 1.50 | 0.04 |
| Cameras of one plan in identifier order | 231 | 44 | 1.97 | 0.27 |
| Last 100 alarms of the site | 408 | 404 | 0.85 | 0.82 |
| Timeline with a selected binding | 3,849 | 292 | 20.42 | 17.59 |
| First full page of a wide timeline request | 39,012 | 1,220 | 94.89 | 65.64 |

## Changes and measurement limits

- Two partial indexes of the map queue contain only pending sources and jobs available to the worker. Paused downloads stay in the table. A separate index on `source_id` serves the reference lookup and the cascading delete. An index on site, plan and point serves the marker selection.
- Archive search computes the retention boundary once per request. To classify the segments, it reads once the intervals that intersect the returned page. The priority of a recording over a gap, the half-open bounds, the binding filter and the limit of 1,001 segments are kept. Intersections in the whole requested window are still taken into account to find the page boundary.
- The function that advances the retention boundary performs one `INSERT ... ON CONFLICT`. The update condition keeps the boundary monotonic and avoids writing a new row version when the boundary is already further. The test checks the value and the physical row identifier in this case.
- The alarm view returns the handling state directly. The feed snapshot and the delivery of changes use it without a second join with the alarm table. The existing indexes on site, point and position are kept. The time to get 100 alarms changed little in this profile.

The measurements apply to reads on a synthetically filled database. They do not determine the number of cameras recorded at the same time, p95/p99 latencies under concurrent writes, network latency or disk load on a specific site. The four new indexes need space, and when the indexed state of a job or a placement changes, PostgreSQL maintains the corresponding index.
