---
title: "Recovery and Checkpoints"
weight: 8
---

The [previous post](/posts/transactions-and-the-write-ahead-log/) ended with committed page images in the WAL while the main database file could still contain older versions. Recovery combines those two sources into the latest committed view when DanDB opens the database.

A checkpoint handles the other half of that relationship. It copies the committed WAL-backed state into the main file, synchronizes that file, and then resets the WAL. Both operations work with complete page images. DanDB does not rerun SQL or repeat the B+ tree changes that originally produced them.

## Opening the Database

DanDB treats the database file and its adjacent `.wal` file as one stored database. Before reading either one, `Pager::open` acquires an exclusive lock associated with the main file. A second DanDB process cannot open the same database while that lock is held. The lock coordinates DanDB processes, but it does not prevent unrelated software from changing the files directly.

The main file is opened first. Its size must be a whole number of 4096-byte pages, its header must pass format and checksum validation, and the page count stored in that header cannot extend beyond the physical file. A file may contain additional complete pages beyond the stored count. That case matters when a checkpoint enlarged the file but did not finish updating its committed header.

DanDB needs a valid main header even when newer page images exist in the WAL. The header contains the database ID used to verify that the WAL beside it belongs to the same database. As a result, the current recovery path cannot rebuild an unreadable main header from the WAL alone.

The WAL is then opened or created. An existing WAL must have a valid header with the same database ID. A missing WAL receives a new empty header for that database. This also means the two files cannot be managed independently before a checkpoint. Deleting or moving the WAL can discard committed changes that have not reached the main file, and an empty replacement does not reveal that those changes existed.

`Pager::open_or_create` creates a new database only when opening reports that the main file does not exist. A damaged header, mismatched WAL, or another opening error is returned to the caller instead of being treated as an empty database.

## WAL Scanning

Once the headers have been validated, `WalScanner` reads records in order after the WAL header. It keeps page frames pending until a commit record proves that they form one complete transaction.

The first page frame establishes the pending transaction ID. Every following frame before the commit must use that same ID. The scanner records each frame's page ID and file offset, but none of those offsets is considered committed yet. Interleaved transaction IDs are rejected because the current format expects one consecutive group of frames per transaction.

A commit record accepts the group only when its transaction ID matches and its frame count equals the number of pending frames. The scanner then adds those frame offsets to `latest_committed_frame_offsets`. If a later committed transaction contains another image for the same page ID, its offset replaces the earlier one. Recovery therefore needs only the latest committed image for each page, even when the WAL contains a longer history for that page.

`valid_wal_end_offset` advances only after an accepted commit record. Page frames that reach the end of the file without a matching commit never enter the recovered view. A commit with no page frames is also a valid boundary when no transaction is pending, which matches DanDB's ability to commit a transaction that changed no pages.

This scan reduces the WAL to two results that opening needs. One identifies the latest committed frame for each changed page. The other identifies the last byte backed by a complete accepted commit.

## Damaged WAL Tails

DanDB distinguishes an unfinished tail from damage that would require guessing about later records. If the file ends with only part of a record, scanning stops at the last accepted commit. The same rule removes complete page frames that have no matching commit record. Neither case has established a committed transaction after that boundary.

A recognizable page frame or commit record can also fail its own validation near the end of the file. The scanner marks the remaining region as damaged and discards any pending frames. If the scan reaches the end without finding a later valid commit, that damaged region is treated as an ignored tail.

When `WalManager` opens the WAL, it truncates ignored bytes back to `valid_wal_end_offset` and synchronizes the truncated file before allowing new records to be appended. New transactions therefore begin immediately after the last complete commit rather than after bytes that recovery already rejected.

The rule becomes stricter if a valid commit appears after a damaged record. DanDB rejects the WAL instead of trying to resume from that point. The damaged region may have contained frames needed by the later commit, so accepting it could connect a commit record to the wrong page images.

Some inconsistencies are rejected immediately rather than treated as an unfinished tail. These include an unknown record type with enough bytes to form a record, a commit with the wrong frame count, interleaved transaction IDs, and a WAL whose database ID does not match the main file. DanDB can discard an incomplete final transaction, but it does not claim to recover safely from every form of file corruption.

## Recovered Pages

After scanning, `Pager::open` reads the complete page frame at every offset in `latest_committed_frame_offsets`. Each decoded frame must contain the page ID associated with that offset. A mismatch is reported as corruption instead of placing the image under a different ID.

Page zero takes a separate path because it represents the database header. DanDB decodes its recovered image into `DatabaseHeader`, so a committed page count or root-page change becomes visible even when the main header is older. The pager also stores that recovered value as its committed header.

Every other recovered image enters `recovered_pages_`, keyed by page ID. This map is a committed overlay above the main file, not another cache. It contains only the latest page images accepted by the WAL scan. Uncommitted frames never reach it.

The buffer pool starts empty. When normal page access later misses the cache, the pager checks `recovered_pages_` before reading the older copy from the main file. This is the [same lookup order](/posts/the-pager-and-buffer-pool/) used after a commit in the current process.

Catalog loading happens only after the pager has reconstructed this view. Its system tables are read through the same page interface as user tables, so a table or index committed only to the WAL is present when the catalog is loaded. Recovery does not need a separate metadata replay path.

Opening therefore makes committed WAL state usable without immediately modifying the main file. The overlay remains the newer source for those page IDs until a checkpoint succeeds.

## Checkpoints

A checkpoint copies the pager's current committed view into the main database file. It is separate from commit. Transactions can finish and survive reopening while their page images remain in the WAL.

`Pager::checkpoint` performs the operation in this order:

1. Ensure the main file can hold the page count in `committed_header_`.
2. Write every page in `recovered_pages_` to its page ID in the main file.
3. Write `committed_header_` as page zero.
4. Synchronize the main database file.
5. Reset the WAL to its valid header and synchronize it.
6. Clear `recovered_pages_`.

The checkpoint uses `committed_header_` rather than the pager's current mutable header. This distinction allows `CHECKPOINT` during an active transaction without copying that transaction's uncommitted allocation or metadata changes into the main file.

Normal pages follow the same boundary. Dirty page images owned by the active transaction remain in the buffer pool. The checkpoint writes only `recovered_pages_`, which contains images installed by earlier successful commits. The active transaction can still commit or roll back after the checkpoint.

After a successful checkpoint, the main file contains the current committed page images and the WAL contains only its header. The recovered overlay is no longer needed, so DanDB clears it. Clean pages already present in the buffer pool may remain there because clearing the overlay does not require rebuilding the cache.

Full-page WAL records keep this copy direct. Each overlay entry is already a complete replacement image for one page. The cost is equally direct. A checkpoint writes the complete page even when the original transaction changed only a small part of it.

## Checkpoint Failures

The ordering matters most when a checkpoint cannot finish. If resizing the main file, writing a recovered page, writing the committed header, or synchronizing the main file fails, DanDB returns the error without resetting the WAL or clearing `recovered_pages_`.

The main file may contain only part of the attempted checkpoint at that point. The WAL still contains the accepted committed page images, so the recovery path retains the source that existed before the operation began. DanDB does not erase that source merely because some writes to the main file succeeded.

The main-file validation rule supports one specific interrupted state. A physical file may contain more complete pages than its valid header declares. Those surplus pages are not considered part of the logical database merely because an earlier resize reached disk. The committed header continues to define the page count used by DanDB.

This does not make arbitrary partial writes harmless. The main header must still decode correctly, and any committed WAL records used during reopening must pass the scanner's rules.

Only after the main file has synchronized successfully does DanDB reset the WAL. It clears the in-memory overlay only after that reset also succeeds. If resetting the WAL fails, the checkpoint returns the error and keeps `recovered_pages_` available in the current process.

These rules describe the failure boundaries implemented and tested by DanDB. They do not turn every possible operating-system, hardware, or external file failure into a recoverable case.

## Closing the Database

Closing does not perform a checkpoint. `Pager::close` closes the WAL and main-file handles, then releases the exclusive lock. A database may therefore close with its latest committed page images still stored only in the WAL.

The next open scans those records and reconstructs the same committed view. This is also how DanDB resolves the `Unresolved` state from the previous post. Instead of trusting the interrupted process's in-memory assumption, reopening bases the outcome on the complete commit boundaries that can actually be validated in the WAL.

After a successful close, calling `close` again has no effect. The database files remain unchanged until another open, transaction, or explicit checkpoint operates on them.

Opening combines a valid main file with the latest complete committed page images in its WAL. A successful checkpoint makes those sources agree by synchronizing the committed images into the main file before resetting the log. In both directions, DanDB accepts or discards page images through an explicit commit boundary.
