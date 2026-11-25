-- 서비스 저장소 레코드 — (표, 키) → 바이트 + 판. 키는 ASCII, 바이트 순서로 정렬된다({{keytext}}).
CREATE TABLE sw_record (
    tbl {{keytext}} NOT NULL,
    rkey {{keytext}} NOT NULL,
    bytes {{blob}} NOT NULL,
    version BIGINT NOT NULL,
    PRIMARY KEY ( tbl, rkey )
);
-- 커밋 판 — 시퀀스(트랜잭션 밖에서 오른다 — 되돌린 트랜잭션의 판은 빈틈으로 남는다).
CREATE SEQUENCE sw_commit_version;
