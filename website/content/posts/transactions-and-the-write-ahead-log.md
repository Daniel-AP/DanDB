---
title: "Transactions and the Write-Ahead Log"
weight: 7
---

The [previous post](/posts/the-pager-and-buffer-pool/) ended with dirty pages held in the buffer pool. The transaction path decides whether those changes become committed state or are rolled back. DanDB records a successful outcome in its write-ahead log (WAL) before the main database file is updated.

DanDB handles both outcomes at the page level. It keeps complete page images for rollback in memory and writes complete page images to the WAL during commit. The transaction layer does not need to replay an `INSERT`, reverse a B+ tree split, or understand what any changed byte represents.

## Explicit and Automatic Transactions

DanDB exposes explicit transactions through `BEGIN`, `COMMIT`, and `ROLLBACK`. After `BEGIN`, later statements use the same transaction until one of the two closing commands succeeds. A table change, its secondary-index updates, and any related catalog changes can therefore share one outcome across several statements.

A mutating statement executed without `BEGIN` still needs the same guarantees. If no transaction is active, the code running that statement starts an internal transaction and becomes responsible for finishing it. Success leads to commit. A failure before commit leads to immediate rollback, followed by the original error being returned to the caller.

An ordinary `SELECT` outside an explicit transaction does not start an internal transaction. Inside an explicit transaction, reads use the same pager as writes, so they can see the dirty page images already held in the buffer pool. If the transaction changed the catalog, lookups also use the [staged catalog state](/posts/the-catalog-and-data-model/) created for those changes.

`Pager` owns only one transaction state. Calling `BEGIN` while any transaction is already present is rejected rather than creating a nested transaction. DanDB also has no savepoints. An explicit transaction groups all of its page changes under one final commit or rollback.

## Transaction State

`Pager` keeps the current transaction in one `TransactionState`. Its `transaction_id` connects the in-memory work to the records written during commit. Three collections describe which page images are needed for each possible outcome.

`dirty_page_ids` contains the pages whose current images commit must capture. `new_page_ids` identifies pages allocated by the transaction, which rollback must remove rather than restore. `original_pages` maps an existing page ID to the complete image captured before that page was first opened for modification.

Dirty IDs and original images answer different questions. Commit needs the latest image after every change. Rollback needs the image that existed before the first change. If one transaction modifies the same page several times, its ID appears once in the dirty set and its original image is copied only once.

The status is one of four exact values. `Inactive` means there is no current transaction. `Active` allows normal work. `Failed` and `Unresolved` both keep a transaction present, but they permit different next actions. Their distinction becomes important when an operation cannot complete.

`TransactionState::in_transaction` returns true for `Active`, `Failed`, and `Unresolved`. Only a successful commit or rollback clears the tracked page state and returns it to `Inactive`.

## Tracking Changes

For an existing page, tracking starts before the caller receives mutable access. `PageHandle::mutable_page` first calls `mark_dirty`, which verifies that the transaction is active and is neither `Failed` nor `Unresolved`. The pager then copies the page's current 4096-byte image into `original_pages` and adds its ID to `dirty_page_ids`.

That order is important. If the mutable pointer were returned first, the stored original could already contain part of the new state. Capturing it before access makes it a usable rollback image. A later call for the same page finds its existing entry in `original_pages`, so it does not replace the before-image with a newer copy.

The database header follows the same transaction outcome, but it is not held as a normal buffer-pool page. When the header first changes, `Pager` encodes the current `DatabaseHeader` into a page-sized before-image. When commit needs the latest header, it encodes the updated object again as the image written to the WAL.

A newly allocated page has no previous image. `Pager::new_page` adds its ID to both `new_page_ids` and `dirty_page_ids`, then marks its buffer frame dirty. Rollback can identify that page as new and discard it, while commit can treat its current image like every other dirty page.

This is page-level tracking. DanDB does not collect a list of inserted rows, changed columns, or B+ tree operations for later reversal. Keeping full before-images makes rollback independent of the operation that changed a page. The cost is another 4096-byte in-memory copy for every existing page first modified by the transaction, in addition to the dirty pages that must remain in the buffer pool.

## Write-Ahead Log

The WAL lives beside the database at the database path plus `.wal`. Its 64-byte header records the WAL format version, page size, and database ID, along with fixed identifying fields and a checksum. The database ID binds the log to the database file whose page images it contains.

DanDB appends two kinds of records after that header:

- A page frame identifies the transaction and page, then stores the complete 4096-byte after-image and a checksum. The complete record is 4136 bytes.
- A commit record identifies the transaction, stores the number of preceding page frames that belong to it, and ends with its own checksum. The complete record is 32 bytes.

There is no separate begin record and no record for an individual SQL or B+ tree operation. One committed transaction appears as consecutive page frames followed by its commit record. The frame count gives that final record an exact claim about how many page images belong to the transaction.

This makes the WAL a full-page redo log. Its after-images can reconstruct committed pages by page ID without executing the original SQL or repeating tree mutations. The before-images used by ordinary rollback stay in `TransactionState` and are not written as undo records.

The choice keeps the persistent record format direct, but it does have a cost. Changing a few bytes still produces a 4136-byte page-frame record. The checksums can detect a damaged header or record, but they do not authenticate the file and they do not make unwritten data durable. Commit still has to synchronize the WAL.

The exact byte offsets remain in the [file-format reference](https://github.com/Daniel-AP/DanDB/blob/main/docs/file-format.txt). How DanDB scans these records after opening a database belongs to the recovery path, not to the commit path described here.

## Commit

`Pager::commit_transaction` first requires a current transaction in the `Active` state. It rejects commit when there is no transaction or when the status is `Failed` or `Unresolved`.

Every dirty data page must also be unpinned. A live `PageHandle` means another part of DanDB may still be reading or changing the frame, so the pager cannot treat its current bytes as a stable commit image. The database header is checked through its separate decoded representation rather than a buffer-pool pin.

Once those conditions hold, commit follows this order:

1. Capture the current image of every dirty page, including an encoded header image if the header changed.
2. Append one WAL page frame for each captured image.
3. Append a commit record with the transaction ID and page-frame count.
4. Synchronize the WAL file.
5. Install the header and page images in the pager's committed view.
6. Clear the dirty flag on the cached data pages.
7. Clear `TransactionState` back to `Inactive`.

The order defines the commit boundary. DanDB does not report success or expose the new images as committed until the page frames, commit record, and WAL synchronization all succeed. The main database file is not changed by this operation.

After synchronization, non-header images are stored in the pager's `recovered_pages_` map. This is the committed source that page lookup checks ahead of the main file. If the header changed, `committed_header_` receives its new value. The dirty buffer frames can then become replaceable without losing committed data because the pager has another source for their images.

Catalog publication happens after pager commit. If the transaction changed tables or indexes, DanDB promotes the staged catalog state only after the system-table page images have committed. Publishing it earlier could expose metadata whose stored representation had not reached the same outcome.

Ordinary commit therefore makes the WAL the durable source for the new page images. A later checkpoint can move that state to the main database file, but it is not part of committing the transaction.

## Rollback

Rollback uses the state collected before and during mutation. Before changing any frame, `Pager::rollback_transaction` checks that every existing page it must restore and every new page it must discard is still cached and unpinned.

Performing all of those checks first prevents a partial rollback. The pager does not restore several pages and then discover that the next required frame is still owned by a live handle.

After the checks pass, rollback performs the inverse of the in-memory changes:

1. Walk through `original_pages`. A saved header image restores the decoded database header, while a normal page image replaces the corresponding buffer frame and clears its dirty state.
2. Discard every page listed in `new_page_ids` and return its frame to the unused collection.
3. Clear `TransactionState` back to `Inactive`.

Any staged catalog state is discarded only after pager rollback succeeds. The in-memory metadata and its system-table pages therefore return to their earlier state together.

Normal rollback writes no undo record to the WAL. The changed page images never replaced their committed versions in the main database file, and their before-images are already available in memory. This keeps rollback independent of logical row and tree operations, but the required before-images consume memory until the transaction ends.

Rollback always applies to the complete transaction. DanDB cannot return to a point between two statements because it does not maintain savepoints or a sequence of logical undo operations.

## Failure States

When DanDB owns the automatic transaction for one mutating statement, a pre-commit failure takes the direct path. It rolls back the pager, discards staged catalog state, and returns the operation's error. After rollback succeeds, no transaction remains for the caller to manage.

An error inside an explicit transaction has a different result. A final lexer, parser, binder, constraint, or execution error changes the status to `Failed`. Later statements are rejected, including `COMMIT`, and `ROLLBACK` is required. This prevents work from continuing after one statement may have changed only part of the transaction's in-memory state.

`IncompleteInput` is handled separately. An unfinished multiline statement has not reached a final error, so it leaves the explicit transaction usable while the command-line interface collects the remaining input.

`Unresolved` has a narrower meaning. `Pager` enters this state when writing or synchronizing the WAL fails during commit. Some WAL bytes may have reached the file even though the operation returned an error, so the current process cannot safely choose between treating the transaction as committed and restoring its in-memory before-images.

DanDB therefore rejects further SQL, another commit attempt, rollback, and additional page changes while the status is `Unresolved`. The database must be closed and reopened. Guessing that the commit failed could contradict a complete transaction that the WAL presents after reopening.

DanDB treats the page images as committed only after writing their matching commit record and successfully synchronizing the WAL. Rollback instead restores the before-images kept in memory. The next post explains how recovery reads the WAL and decides which committed page images to use.
