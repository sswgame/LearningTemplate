# 영속 레코드의 모르는 필드 보관

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: F-3 · 진행: 1/2

영속 레코드(`ServiceRecordUtil::readDurable`)는 배포 중 옛 빌드가 새 빌드의 레코드를 읽으면 모르는 필드 때문에 거절한다(조용히 지우지 않고 요청 실패). 모르는 필드를 버리고 읽게 하면 옛 빌드가 고쳐 다시 쓸 때 새 빌드의 값이 지워진다.

## 들어간 것

담아 다시 싣는 구조: `BinarySerializer::serializeKeepingUnknown` · `serializeVersionedKeepingUnknown` 이 읽을 때 담은 모르는 칸(이름이 빈 orphan)을 (이름 해시, 기록 타입 해시, 값) 그대로 뒤에 싣는다(언리얼 `FPropertyTag` 보관, protobuf unknown fields 와 같은 자리).
레코드 쪽 창구는 `ServiceRecordUtil::readDurableKeepingUnknown` · `encodeKeepingUnknown` · `ServiceRecordUnknownFields` 다. 아는 칸 · 읽지 못한 아는 칸(모르는 열거자) · 다른 와이어 버전은 고쳐 싣지 않고 거절한다. 테스트는 `ServiceRecordUnknownFieldsTest`(빌드 · 실행 전).

## 남은 것

- 호출부 옮기기: 읽고 고쳐 다시 쓰는 영속 레코드(`LedgerItemInstance` · `ServiceReport` · 채팅 · LiveOps · 서버 디렉터리 점검 창과 공지 · 거래)를 `readDurableKeepingUnknown` + `encodeKeepingUnknown` 으로 바꾸고, 그 레코드의 읽기가 모르는 칸을 받게 한다. 지금의 `readDurable` 은 모르는 칸을 그대로 거절한다.

함정: 맨 바깥 칸만 담는다 — 안쪽 구조체에 새로 더한 칸은 담지 않으므로 그 칸을 더할 때는 레코드 버전을 올린다. 버전이 다르면 보관 읽기는 이관과 섞지 않고 거절한다. 캐시 레코드(`readCache`)는 이미 서버 버스와 같은 혼합 빌드 읽기다([서버 버스 계획](ServerBusFormat.md) 4절).
