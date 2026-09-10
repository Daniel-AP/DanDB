---
title: "The Pager and Buffer Pool"
weight: 6
---

The [previous post](/posts/tables-and-indexes-as-b-trees/) followed DanDB's B+ trees down to individual page IDs. A tree knows which page it needs, but it does not know whether that page is already in memory or has to be loaded from persistent storage. It passes that decision to `Pager`.

The pager returns more than a pointer to some bytes. It returns a handle that keeps the page available while the tree uses it. Behind that handle, the buffer pool stores a fixed number of pages and decides which clean, unused page can be replaced when another one is needed.

## Pages

A `Page` in DanDB contains a `PageId` and an array of exactly 4096 bytes. Constructing one starts those bytes at zero. The object does not know whether its contents represent a leaf, an internal B+ tree node, or another stored structure. That interpretation belongs to the code that requested the page.

Page IDs describe locations in the database rather than slots in memory. Page zero is reserved for the database header, and normal pages begin at ID one. `Pager::get_page` deliberately rejects page zero because the pager keeps the decoded `DatabaseHeader` through a separate interface.

This leaves the same `Page` type available to every page-based structure without making the storage layer depend on B+ tree fields or catalog rows.

## Pager

`BTree` works with two pager operations. `get_page` opens a page that already exists, while `new_page` creates the next page. Both return a `PageHandle`, so the tree never calls `DiskManager` or `BufferPoolManager` directly.

That boundary keeps two decisions out of the tree code. `Pager` decides where the current page image comes from, and the buffer pool decides which memory frame will hold it. The buffer pool itself does not read or write either database file. It only manages `Page` objects supplied by the pager.

## Page Lookup

`Pager::get_page` first asks `BufferPoolManager` for the requested ID. At this level, `get_page` means "get this page if it is already cached." A hit finds the page's frame, pins it, and returns a `PagePin` that the pager wraps in a `PageHandle`.

A `NotFound` result is a cache miss rather than a missing database page. The pager then chooses the current page image. It first checks its map of committed page images that are newer than the main database file. Otherwise, it asks `DiskManager` to read the page from that file. How the committed map is populated belongs to the transaction and recovery posts.

Once the pager has the image, it gives the complete `Page` to the buffer pool. The buffer pool places the copy in a frame, pins that frame, and returns it through the same handle used for a cache hit. Code above the pager therefore uses one interface regardless of where the bytes were found.

Errors other than `NotFound` are not treated as cache misses. An invalid page ID or another buffer-pool failure returns to the caller instead of causing an unrelated disk read.

## Buffer Pool

`BufferPoolManager` receives its capacity when the pager is created or opened. It allocates that number of `BufferFrame` objects up front and does not add more frames later. The normal `Database` path currently uses 4096 frames.

Each frame stores one `Page`, a pin count, and a dirty flag. The manager also keeps a collection of unused frame IDs and a map from each cached `PageId` to the frame that currently holds it.

The map lets another request for the same page find its existing frame. It also prevents the manager from caching a second copy under the same page ID. When a different page later takes over a frame, the old mapping is removed before the new one is installed.

With 4096-byte pages, the configured 4096 frames hold 16 MiB of page bytes, not counting the frame objects and container overhead. This is a fixed boundary rather than a target the manager tries to maintain dynamically.

## Page Handles

The buffer pool protects access to a frame with `PagePin`. The pager wraps that object in `PageHandle`, which is the type used by B+ tree code. Both types are move-only. Moving one transfers its ownership, while copying is disabled so two independent objects cannot accidentally release the same pin.

Creating a `PagePin` means that one caller is using the frame. When the pin leaves scope, its destructor calls the buffer pool exactly once to release that ownership. A second request for the same page creates another pin and increments the frame's count, so the frame remains protected until every caller has finished.

`PageHandle` also separates reads from changes. Its `page()` method returns a const page. Code that needs to modify the bytes must call `mutable_page()`, which first asks `Pager` to register the change. If that registration fails, the caller receives an error instead of a mutable pointer.

The handle then marks its internal pin as dirty. While the handle remains alive, the pin itself prevents replacement. When the handle is destroyed, `PagePin` releases the pin and passes the dirty state to the frame. This makes the lifetime of ordinary page access follow the lifetime of a C++ object instead of relying on each return path to call `unpin_page` manually.

## Eviction

When the buffer pool caches a page, it first uses an unused frame. Once every frame has held a page, it asks `LRUReplacer` for a victim.

DanDB's replacer tracks frame IDs that are currently eligible for replacement. Pinning a frame removes it from that set. Releasing the final pin adds the frame back only when it is clean. The newly eligible frame goes to the front of the LRU list, and replacement takes the frame at the back.

Replacing a frame removes the old page-to-frame mapping and resets the frame's page, pin count, and dirty flag. The requested page is then copied into that slot, pinned, and added to the map under its own page ID.

The eligibility rule is more important than the list itself. A pinned page cannot be replaced because a handle may still be reading or changing its bytes. A dirty page cannot be replaced because its changes have not been committed or rolled back. If every frame is in one of those states, the replacer has no victim and the request fails rather than reusing a frame whose contents are still needed.

## Dirty Pages

In DanDB, a dirty page is a cached page created or opened for modification by the current transaction whose new image has not yet been committed to the write-ahead log.

An existing page enters this state when `PageHandle::mark_dirty` registers it for mutation. A new page starts dirty when `Pager::new_page` allocates it because no committed copy exists yet. `PageHandle::mutable_page` calls `mark_dirty` before returning a mutable pointer.

The handle first records the state on `PagePin` while the frame is still pinned. When the pin is released, `BufferPoolManager::unpin_page` decreases the count and transfers that state to `BufferFrame`. A clean frame whose count reaches zero becomes replaceable. A dirty frame whose count reaches zero stays outside the LRU replacer.

After a successful commit, the page image becomes part of the committed WAL-backed view and `Pager` clears the dirty flag. A rollback instead restores the previous committed image or removes a newly allocated page. The main database file may still contain an older image until a checkpoint, so clean does not mean that the frame matches that file. It means the pager can reproduce the page from its committed state.

This gives DanDB a concrete capacity limit. A transaction cannot keep more dirty data pages than the buffer pool has frames because dirty frames cannot be evicted. The rule keeps those page images available for the transaction layer, but it also means a mutation can run out of frames even when the pool still contains pages whose handles have already been released. The next post explains how commit and rollback resolve that state.

## Page Allocation

`Pager::new_page` creates pages in increasing ID order. It uses the current page count from `DatabaseHeader` as the new ID, constructs a zero-filled `Page`, and asks the buffer pool to cache and pin it. It then advances the header's page count and marks both the new page and the header as changed.

`Pager::new_page` only succeeds while a transaction is active. If no transaction has started, or the current transaction is `Failed` or `Unresolved`, allocation returns a `TransactionError`. The new page still follows the same handle and dirty-page rules used for an existing page, and creating it does not write it directly to the main database file.

This allocation path is append-only. DanDB does not maintain a free-page list, so pages left unreachable after the B+ tree merges described in the previous post are not selected for later allocations. A new page always takes the next ID instead.

At this point, a page ID has become a pinned handle backed by one of a fixed number of frames. Reads keep that frame protected, changes keep it dirty, and releasing the handle makes a clean page eligible for another request. What remains is deciding whether a group of dirty pages commits or rolls back, and how that decision reaches the write-ahead log.
