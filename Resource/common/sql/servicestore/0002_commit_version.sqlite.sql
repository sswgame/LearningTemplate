-- 커밋 판 — 커밋마다 하나 오른다(되돌린 트랜잭션의 판은 다시 쓰이지 않아도 된다).
CREATE TABLE sw_store_counter ( name TEXT PRIMARY KEY, value BIGINT NOT NULL );
INSERT INTO sw_store_counter ( name, value ) VALUES ( 'commit_version', 0 );
