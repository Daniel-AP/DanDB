CREATE TABLE tasks (id INT64 PRIMARY KEY, title STRING(64), status STRING(16) NOT NULL);

INSERT INTO tasks VALUES (1, 'Write lexer', 'todo');
INSERT INTO tasks VALUES (2, 'Add smoke script', 'todo');
INSERT INTO tasks VALUES (3, 'Review output', 'done');

SELECT * FROM tasks;

UPDATE tasks SET status = 'done' WHERE id = 2;

CREATE INDEX tasks_by_status ON tasks (status);
SELECT id, title, status FROM tasks WHERE status = 'done';

DELETE FROM tasks WHERE id = 1;
SELECT * FROM tasks;

CREATE TABLE drafts (id INT64 PRIMARY KEY, description STRING(64));

BEGIN;
INSERT INTO drafts VALUES (1, 'Temporary draft');
ROLLBACK;
SELECT * FROM drafts;

CHECKPOINT;
