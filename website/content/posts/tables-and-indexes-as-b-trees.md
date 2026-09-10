---
title: "Tables and Indexes as B+ Trees"
weight: 5
---

The [previous post](/posts/the-catalog-and-data-model/) ends with two byte sequences ready for storage. `RowCodec` has encoded a complete row, while `KeyCodec` has encoded a key whose byte order matches its logical order. DanDB brings them together through `BTree`, the structure used for table rows and secondary indexes.

The implementation is shared, but the meaning of an entry depends on the tree being opened. That distinction shapes how DanDB enforces uniqueness, performs indexed reads, and keeps indexes aligned with their table.

## Tables and Indexes

A table tree stores an encoded primary key as its key and the complete encoded row as its value. For the `users` table used in the earlier posts, the entry for `id = 1` connects the encoded `INT64` key to the row containing the user's remaining values. Looking up a primary key therefore returns everything needed to decode that row.

A unique secondary index uses a different layout. Its key is the encoded value of the indexed column, and its value is the row's encoded primary key. Since `BTree::insert` rejects an existing key, trying to insert the same indexed value twice becomes a unique-constraint violation.

A non-unique secondary index must allow several rows to have the same indexed value without allowing duplicate tree keys. DanDB solves this by appending the primary key to the indexed value. An index on `active`, for example, stores a key composed of the encoded Boolean followed by the encoded `id`. Rows with the same `active` value stay next to one another, while the primary-key suffix keeps each entry distinct. The value repeats the primary key so every secondary-index entry can be consumed through the same lookup path.

`Database::open_table_tree` and `Database::open_index_tree` provide the sizes required by each layout. A table tree uses the primary-key width and the schema's row size. A unique index uses the indexed-column width and the primary-key width. A non-unique index adds the primary-key width to its key. The B+ tree code can remain unaware of schemas because it receives fixed key and value sizes when a tree is opened.

## Page Structure

Every node in a DanDB B+ tree occupies one 4096-byte page. The first 64 bytes form a common header, and the rest of the page holds a contiguous array of fixed-size entries. The header records the page kind, root flag, key count, parent page ID, leaf links, first child page ID, and the key and value sizes expected by the tree.

`BTreePage::open` validates this stored header before the page is treated as a node. It rejects an invalid page size or kind, unsupported flag bits, an unexpected header size, nonzero reserved bytes, zero key or value sizes, and a key count above the calculated capacity. `BTreeLeafPage::open` and `BTreeInternalPage::open` then check that the page has the expected kind before exposing fields specific to that representation.

A leaf entry places its key bytes immediately before its value bytes. Its size is therefore `key_size + value_size`. The `users` schema from the previous post produces 74-byte rows and uses an 8-byte primary key. A table leaf can hold 49 of those 82-byte entries after accounting for the header.

DanDB's internal pages use another fixed layout. The first child page ID lives in the header. Each entry after it contains a separator key followed by the page ID of the child to that separator's right. Internal capacity depends on `key_size + 8`, not on the tree's value size, because internal pages do not store rows or primary-key values.

The separator has one precise meaning in this representation. It is the smallest key reachable through its right child. Parent page IDs connect children back to their parent, while leaf pages also keep previous and next page IDs for scans. The exact offsets remain in the [file-format reference](https://github.com/Daniel-AP/DanDB/blob/main/docs/file-format.txt).

## Key Lookup

`BTree` keeps the root page ID and the fixed key and value sizes supplied when it is created or opened. `find` first checks the requested key size, then uses the root page ID to begin the lookup.

The important rule is implemented by `BTreeInternalPage::child_index_for_key`. DanDB compares the requested bytes with the stored separators. Values below the first separator use the first child from the header. After that, the selected child is the right child paired with the last separator that is less than or equal to the requested key.

An exact match with a separator goes right because that separator is the minimum key in the right subtree. Once the selected page is a leaf, DanDB finds the first position whose stored key is not less than the requested key, then confirms an exact byte match before returning the value.

These comparisons can remain bytewise because `KeyCodec` has already handled the type-specific ordering described in the previous post. The tree does not need separate comparison code for signed integers, strings, and Boolean values.

## Scans

DanDB represents a scan with `BTreeCursor`. The cursor stores a current leaf page ID, an entry index inside that leaf, and an optional exclusive upper bound. It returns one copied key and value pair from each call to `next` rather than constructing a collection containing every matching entry.

`BTree::scan` starts the cursor at the first entry in the leftmost leaf. `BTree::scan_range` instead uses an optional lower bound to find the initial leaf and position. The lower bound is inclusive. If an upper bound is present, the cursor owns a copy of those bytes and finishes before returning a key greater than or equal to it.

When the cursor reaches the end of one leaf, it continues from the next leaf page ID stored in that page's header. Its state is independent of the number of entries that remain, which lets execution consume table or index entries one at a time.

The half-open range `[lower, upper)` is also the contract used by DanDB's access paths. Equality can be represented by starting at one encoded key and stopping at the next possible key. Other comparisons build one or two ranges around the same boundary values. The execution layer decides which ranges it needs, while `BTreeCursor` only enforces their byte boundaries.

## Insertions and Splits

Insertion uses the same leaf position as lookup. Before changing the page, DanDB checks whether that position already contains the key. This single duplicate check serves different constraints depending on the tree. It protects table primary keys, enforces unique secondary indexes, and remains compatible with non-unique indexes because their keys include the primary-key suffix.

If the leaf has space, `BTreeLeafPage::insert_entry` shifts the later fixed-size entries and writes the new key and value into the resulting slot. A full leaf follows a more deliberate path. DanDB builds a temporary ordered collection containing the existing entries and the new entry, creates a right leaf, then rebuilds the left and right halves from that collection.

This approach performs temporary allocations and copies during a split. In return, the ordering and split point remain visible in ordinary container operations instead of depending on a sequence of overlapping byte moves across two pages. The implementation then connects the two halves to the existing leaf chain and updates the previous link of the leaf that originally followed them.

The first key in the new right leaf becomes the separator returned to the parent. It remains in the leaf because it still identifies a stored value there. If the parent has capacity, the separator and right child page ID are inserted into its ordered entry array.

When an internal page is also full, DanDB collects its entries together with the incoming child split. The middle separator is promoted. Entries before it rebuild the left page, entries after it fill a new right page, and the promoted entry's right child becomes that page's first child. Children moved to the new page receive its page ID as their parent.

DanDB handles a root split without changing the tree's root page ID. It copies the old root contents into a newly created left child, rewrites the original root page as an internal page, and places the promoted separator between the left and right children. If the old root was internal, its former children are updated to point to the new left child. If it was a leaf, the leaf links are repaired for the new arrangement.

Keeping the root ID stable matters because table and index descriptors store that ID as the durable entry point to each tree. Growing a tree therefore requires an additional page copy and parent updates, but it does not require rewriting the catalog entry that identifies the tree.

## Deletions and Rebalancing

Deletion first removes the matching key and value from its leaf. The less obvious work begins when that entry affected a boundary recorded by an internal page. If the removed key was the minimum key of a non-leftmost child, DanDB refreshes the corresponding parent separator even when the child still contains enough entries.

For a non-root leaf, the minimum accepted key count is `(capacity + 1) / 2`. For a non-root internal page, it is `capacity / 2`. Falling below those limits causes DanDB to repair the child through its parent.

The implementation first checks whether an adjacent sibling can spare an entry. Leaf repair moves a complete key and value pair and rewrites the separator that describes the affected right child. Internal repair moves the relevant child pointer together with a separator, then updates the parent page ID of the child that changed sides.

If a sibling cannot spare an entry, DanDB combines two adjacent pages. Combining leaves also removes one page from the previous and next leaf chain. Combining internal pages brings the separating key from the parent into the resulting page and updates the parent IDs of the moved children. The parent entry that separated the old pages is then removed, and the same occupancy check can continue upward.

The root receives separate treatment. When an internal root has no separators left, its only child is copied into the original root page. The copied page becomes the root, and its children's parent IDs are updated if it is internal. Root contraction therefore preserves the same durable root page ID used during growth.

The current implementation does not return pages made unreachable by a merge or root contraction to a reusable free-page list. The tree remains structurally consistent, but deleting entries does not reduce or recycle the database file's allocated page count. This is a real storage limitation rather than part of the B+ tree contract.

## Index Lookups

The SQL pipeline post described how execution selects a primary-key path, secondary-index path, or full table scan. A secondary-index path still needs one more step because its tree stores primary keys rather than complete rows.

For a unique index, the encoded predicate value already has the complete tree-key width. For a non-unique index, it is only a prefix because the stored key also contains the row's primary key. `open_secondary_index_cursors` pads that prefix with zero bytes to form the first possible composite key for the indexed value. When the next encoded prefix exists, it becomes the exclusive end of the range after receiving the same padding.

The resulting cursor returns every index entry inside that value range. `consume_secondary_index_cursors` reads the primary key from each entry's value and passes it to `table_tree.find`. Only then does execution receive the complete encoded row for decoding and predicate evaluation.

This layout keeps the row in one authoritative tree instead of copying it into every secondary index. The cost is visible in the read path. Reaching a row through a secondary index requires an index-tree scan followed by a table-tree lookup.

## Index Maintenance

Creating a secondary index cannot stop after allocating an empty tree. DanDB scans the existing table tree, decodes each stored row, derives the indexed key, and inserts that key together with the row's primary key. If the new index is unique, any duplicate encountered during this backfill rejects the operation.

An `INSERT` writes the primary-key and row pair to the table tree, then creates an entry in every secondary-index tree attached to that table. Each index receives the same primary key as its value, but its key follows the unique or non-unique layout described earlier.

An `UPDATE` opens only the secondary indexes whose indexed columns are affected by the assignment. DanDB derives the old and new index keys and skips the index when those byte sequences are equal. Otherwise, it removes the old entry, replaces the row value in the table tree, then inserts the new index entry.

There is one important interaction between index maintenance and scans. If an update found its candidates through a secondary index, changing the indexed value could restructure the same tree that the active cursor is following. DanDB first collects the matching primary keys and row bytes, lets the cursor finish, then applies those updates. `DELETE` follows the same separation for all access paths by collecting its candidates before removing entries from the table and its indexes.

After a successful statement, the table tree and every affected index tree describe the same set of rows. How DanDB makes a group of page changes commit or roll back together belongs to the transaction and logging layer, not to the index algorithms themselves.

