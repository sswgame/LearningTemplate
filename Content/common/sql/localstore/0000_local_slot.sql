-- 클라이언트 로컬 저장 슬롯 — 슬롯 이름 → 봉투 바이트(LocalSlotEnvelope) + 쓴 시각(이 기계의 파일 시계 밀리초). 서버 저장소(servicestore)와 다른 DB 다.
CREATE TABLE sw_local_slot (
    slot {{keytext}} NOT NULL PRIMARY KEY,
    bytes {{blob}} NOT NULL,
    written_at_ms BIGINT NOT NULL
);
