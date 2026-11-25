-- 서비스 저장소 레코드 — (표, 키) → 바이트 + 판. 키는 ASCII, 바이트 순서로 정렬된다({{keytext}}).
CREATE TABLE sw_record (
    tbl {{keytext}} NOT NULL,
    rkey {{keytext}} NOT NULL,
    bytes {{blob}} NOT NULL,
    version BIGINT NOT NULL,
    PRIMARY KEY ( tbl, rkey )
);
-- 커밋 판 — 커밋마다 하나 오른다(되돌린 트랜잭션의 판은 다시 쓰이지 않아도 된다).
CREATE TABLE sw_store_counter ( name TEXT PRIMARY KEY, value BIGINT NOT NULL );
INSERT INTO sw_store_counter ( name, value ) VALUES ( 'commit_version', 0 );
