---
title: "Benchmarking"
weight: 9
---

DanDB includes a benchmark suite for observing how the current implementation behaves under controlled workloads. The measurements below cover the B+ tree directly, the complete SQL execution path, and the durable work performed by commits and checkpoints. They describe one project revision on one machine rather than making a general performance claim.

## Measurement Context

| Item | Configuration |
|---|---|
| Processor | Intel Core i7-1255U, 10 cores and 12 logical processors |
| Memory | 15.7 GiB |
| Storage | WD PC SN740 512 GB NVMe SSD |
| Operating system | Windows 11, build 26200 |
| Power plan | Balanced |
| Compiler | g++ 14.2.0 |
| Build | CMake Release preset with benchmarks enabled |
| Google Benchmark | 1.9.5 |
| Project revision | `2c9ca66` |

The benchmark families ran sequentially. Google Benchmark randomly interleaved repetitions within each family to reduce gradual ordering effects. B+ tree and SQL read cases used five repetitions. The selected SQL write cases used nine, while commit and checkpoint used fifteen because file synchronization introduced more variation. The tables report median elapsed time, not the fastest repetition.

Most workloads use 1,000, 10,000, and 100,000 rows or entries. A fixed random seed, `0x6E3A9D17`, keeps lookup and mutation order repeatable. Initial population and other setup work are normally outside the timed region. SQL measurements include the complete `Database::execute` call, while commit and checkpoint explicitly use wall-clock time.

The read tests operate on a populated database that remains open during the measurement. They therefore represent warm, in-process access and may benefit from DanDB's buffer pool and the operating system's file cache. The B+ tree and SQL read families were consistent across repetitions, with elapsed-time coefficients of variation no higher than 7.7 percent. Durable writes were noisier, so their medians are rounded and their variation is discussed where it affects interpretation.

## B+ Tree Performance

These benchmarks call `BTree` directly and avoid the SQL layer. They use 8-byte integer keys, 32-byte values, and a buffer pool that can hold 2,500 pages.

### Building the tree

Each insertion iteration starts with a new tree and ends with a transaction commit. Sequential insertion uses keys in ascending order. Shuffled insertion uses the same keys in a deterministic random order.

| Entries inserted | Sequential | Shuffled |
|---:|---:|---:|
| 1,000 | 1.73 ms | 1.84 ms |
| 10,000 | 15.9 ms | 16.0 ms |
| 100,000 | 183 ms | 197 ms |

Insertion order made little difference under this workload. At 100,000 entries, the shuffled case was about 8 percent slower.

### Point lookups

The lookup benchmark searches for existing keys in a deterministic shuffled order. The tree stays open and populated throughout the timed loop.

| Entries in tree | Median lookup time |
|---:|---:|
| 1,000 | 0.888 µs |
| 10,000 | 0.982 µs |
| 100,000 | 1.56 µs |

The tree grew by 100 times while median lookup time increased by about 76 percent. This is a narrow result for warm lookups with fixed-size keys and values. It does not include SQL parsing, row decoding, or materialization.

### Range scans

Range scans use one tree containing 100,000 entries. Only the number of entries returned changes.

| Entries returned | Median scan time |
|---:|---:|
| 1,000 | 0.276 ms |
| 10,000 | 2.79 ms |
| 50,000 | 14.0 ms |

Once the cursor is positioned, the number of entries returned dominates this workload.

### Mutations and mixed access

The update and erase columns measure one complete pass over the stated number of existing entries in shuffled order. Their final commit is outside the timed region. The mixed workload performs the same number of operations with an 80 percent read and 20 percent update split. It is a synthetic workload rather than a model of a particular application.

| Operations | Random update | Random erase | 80/20 read-update mix |
|---:|---:|---:|---:|
| 1,000 | 0.728 ms | 1.78 ms | 0.740 ms |
| 10,000 | 8.41 ms | 22.0 ms | 8.42 ms |
| 100,000 | 143 ms | 341 ms | 141 ms |

The mixed case stayed close to the update pass despite replacing four out of every five updates with reads.

## SQL Read Performance

The SQL measurements include the full path through `Database::execute`, including statement processing, execution, and result construction. The benchmark table has two `INT64` columns named `id` and `value`. Each point lookup returns one row.

### Point lookups

| Rows | Primary key | Unindexed table scan | Secondary index | Unique secondary index |
|---:|---:|---:|---:|---:|
| 1,000 | 3.40 µs | 411 µs | 4.43 µs | 4.65 µs |
| 10,000 | 3.56 µs | 4,161 µs | 4.66 µs | 4.95 µs |
| 100,000 | 4.20 µs | 40,243 µs | 5.95 µs | 6.03 µs |

The unindexed lookup followed table size, while the three indexed paths remained in the single-digit microsecond range. Secondary-index lookup costs slightly more than primary-key lookup because DanDB first resolves the secondary key to a primary key and then retrieves the table row.

### Range queries

Every query in this table returns 100 rows. The dataset size changes, but the result size does not.

| Rows in table | Primary key | Unindexed table scan | Secondary index | Unique secondary index |
|---:|---:|---:|---:|---:|
| 1,000 | 50.3 µs | 411 µs | 140 µs | 167 µs |
| 10,000 | 49.2 µs | 4,044 µs | 144 µs | 173 µs |
| 100,000 | 49.6 µs | 40,961 µs | 176 µs | 199 µs |

The primary-key range stayed near 50 µs because the ordered tree can seek to the first matching key and then read 100 adjacent entries. Secondary indexes also avoid inspecting the complete table, although each returned index entry still leads to its table row. The unindexed predicate scans the full dataset, so its time follows total table size even though every query returns the same 100 rows.

### Full scans

A full scan returns and materializes every row.

| Rows returned | Median elapsed time |
|---:|---:|
| 1,000 | 0.454 ms |
| 10,000 | 4.53 ms |
| 100,000 | 47.1 ms |

Unlike the fixed-result range queries, this timing includes a growing amount of row decoding and result construction.

## SQL Write Performance

SQL writes are end-to-end measurements. Depending on the case, one call may change the table, maintain a secondary index, write transaction records, and synchronize the write-ahead log (WAL). Those file operations made the write results more variable than the warm read results.

### Autocommit and batching

The autocommit case inserts one row in its own transaction. The batch case sends 100 inserts between an explicit `BEGIN` and `COMMIT` to a table with only its primary index. Both start from a table containing 100,000 rows.

| Mode | Work per measurement | Median elapsed time | Approximate time per row |
|---|---:|---:|---:|
| Autocommit | 1 row | 426 µs | 426 µs |
| Explicit transaction | 100 rows | 971 µs | 9.71 µs |

The per-row value for the batch is the total divided by 100, not a latency percentile. Sharing one commit reduced the average measured cost per inserted row by about 44 times in this workload.

### Maintaining indexes

This comparison inserts a batch of 100 rows into a table that already contains 100,000 rows. The indexed variants must add one secondary entry for every new table row.

| Table configuration | Median time for 100 inserts |
|---|---:|
| Primary index only | 0.971 ms |
| Ordinary secondary index | 1.51 ms |
| Unique secondary index | 1.42 ms |

Maintaining either secondary index added roughly half a millisecond to this batch. The ordinary and unique cases overlap once run-to-run variation is considered, so the small difference between their medians is not evidence that one form is cheaper.

### Updates and deletes

Each update changes one row selected by primary key. The indexed cases change the indexed `value`, which requires replacing its secondary entry. Each delete also selects one row by primary key and removes any corresponding secondary entry.

| Updated value | Median elapsed time |
|---|---:|
| Not indexed | 0.389 ms |
| Ordinary secondary index | 0.611 ms |
| Unique secondary index | 0.544 ms |

| Deleted row | Median elapsed time |
|---|---:|
| Primary index only | 1.42 ms |
| Ordinary secondary index | 1.82 ms |
| Unique secondary index | 1.40 ms |

Updating an indexed value was slower because DanDB also replaces its secondary-index entry. The ordinary secondary-index delete had the highest median at 1.82 ms, but its nine repetitions included a 4.17 ms outlier. That variation makes the exact size of its overhead uncertain. The primary-only and unique-index medians were nearly identical.

## Commit and Checkpoint Performance

Both durability workloads start from a checkpointed B+ tree containing 100,000 entries. Before each timed operation, they update 1, 10, 100, or 1,000 entries selected in deterministic shuffled order. The update count is not a dirty-page count because several entries can occupy the same page.

### Commit

The commit timer covers only `Pager::commit_transaction()`. DanDB writes the changed page images and a commit record to the WAL, then synchronizes that file before the call succeeds.

| Entries updated | Median commit time |
|---:|---:|
| 1 | 0.386 ms |
| 10 | 0.555 ms |
| 100 | 2.36 ms |
| 1,000 | 19.3 ms |

### Checkpoint

The checkpoint timer starts after the same changes have already been committed. `Pager::checkpoint()` applies committed page images to the main database file, synchronizes it, and resets the WAL.

| Entries updated before checkpoint | Median checkpoint time |
|---:|---:|
| 1 | 0.755 ms |
| 10 | 1.26 ms |
| 100 | 5.56 ms |
| 1,000 | 28.3 ms |

File synchronization also produced occasional long-tail measurements. In the one-entry commit case, fourteen repetitions fell between 0.326 and 0.433 ms while one took 2.03 ms. The reported 0.386 ms median is not pulled toward that outlier. The same median-based reporting is used throughout both tables, and the exact values remain specific to this SSD and Windows file synchronization behavior.

## Conclusions

Within these warm, single-process workloads, indexed reads were much less sensitive to table size than scans. Batching amortized the cost of commit, while maintaining secondary indexes added work to inserts and updates. Commit and checkpoint followed the number of changed entries but showed wider timing variation because they synchronize files.

The results are limited to the machine, revision, data shapes, and maximum size described above. The suite does not measure concurrency or multiple processes. Its complete workload definitions are available in the [benchmark source directory](https://github.com/Daniel-AP/DanDB/tree/main/benchmarks).
