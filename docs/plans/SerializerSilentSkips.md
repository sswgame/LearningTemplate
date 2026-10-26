# 직렬화가 조용히 넘기는 곳 (2026-10-21)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: C-66 · 진행: 4/4

**남은 확인(통합에서 확인):** 매니페스트 id `serializer-scene-load`(3절). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

읽기 · 쓰기가 조용히 넘기면 데이터 결함이 숨습니다. 페이로드를 끝까지 읽었는지는 `SerializerUtil::applyPropertyBinary` 하나가 보고, B 코덱 열거형 폭은 열거자 범위입니다. 남은 결함은 **읽기가 실패한 필드에 앞부분이 써진 채 남는 것**이었습니다(읽지 못한 필드를 넘기고 이어 읽는 읽기에서만 반쯤 읽힌 값이 된다).

## 1. 정한 것

- 읽지 못한 필드를 넘기고 이어 읽는 읽기는 그 필드를 **읽기 전 값**으로 남깁니다. 읽다 만 값은 받지 않습니다.
- 임시에 읽고 성공할 때만 옮기는 형식은 쓰지 않았습니다(`PropertyInfo` 에 값 타입의 생성 · 이동 연산이 없어 코드 생성기와 모든 생성 코드를 바꿔야 한다). 대신 읽기 전에 값을 이 빌드의 바이트로 담아 두고(`SerializerUtil::writePropertyBinary` · `applyPropertyBinary` 의 짝) 실패하면 되읽어 되돌립니다.
- 담는 비용은 실패해도 이어 읽는 읽기(소프트, `skipsUnreadableProperties` 를 켠 엄격 = 오브젝트 상태)만 냅니다. 실패하면 객체째 버리는 엄격 읽기는 담지 않습니다.
- 소유 포인터 목록은 담지 않습니다(되돌리면 팩토리가 컴포넌트를 새로 만들어 다른 객체가 된다). 읽다 만 소유 포인터 목록은 객체째 실패입니다. 값을 담지 못하거나 되돌리지 못하면(코덱 결함) 오류를 남기고 객체째 실패하며 기본값으로 넘어가지 않습니다.
- 담기와 되돌리기는 `PropertyReadRollback`(`SerializerUtil.h`) 하나로 세 형식(바이너리 태그 스트림, JSON `readObject`, XML `readXMLIntoInstance`)이 함께 씁니다.
- 되돌리는 단위는 **실패를 받은 필드 하나**입니다. 구조체 칸은 안쪽 읽기가 안쪽 필드만 되돌리므로 칸 자신은 담지 않고, 컨테이너는 원소 단위가 아니라 칸째 되돌립니다(원소 하나만 빼면 바이너리와 다른 값이 되고 다음 저장이 그 원소를 지운다). XML 은 칸을 읽는 동안 쌓인 원소 orphan 을 지우고 **칸 원문 하나**를 orphan 으로 남깁니다(바이너리 · JSON 과 같다).
- 읽기 결과는 **필드 실패와 객체 실패를 형식으로 가릅니다**(S3). 필드 · 구조체 읽기는 `FieldReadResult`(Read · FieldFailed · ObjectRejected), 원소 읽기는 `ContainerReadResult` 에 `ObjectRejected` 를 더했습니다(`ContainerVisitor.h`). 예전에는 둘 다 `bool` 의 false 여서 안쪽의 금지된 필드가 칸 실패로 낮아졌습니다. 받는 쪽은 `default` 없는 `switch` 라 값이 늘면 빌드가 멈춥니다. 필드 경계의 `StreamBroken` 은 그 필드의 실패입니다(`ContainerVisitor::toFieldResult`).
- 담는 대상은 좁히지 않았습니다(S3 에서 따짐) — 내장 벡터 글 처리기(`registerVectorTextHandler`)는 앞 축을 쓴 뒤 다음 축에서 실패하는 반례가 있고, 처리기 등록이 "성공할 때만 쓴다"를 말하지 않아 타입으로 가를 근거가 없습니다. 좁히려면 등록 API 가 그 약속을 갖거나 처리기가 임시에 읽고 옮기게 바꿔야 합니다(측정에서 회귀가 보일 때).
- 바이너리도 같은 형식입니다(S4): `deserializeTagged` · `deserializeValueBinary` · `deserializeNestedContainerBinary` · `applyPropertyBinary` · `tryCoerceBinaryPayload` 가 `FieldReadResult` 를 돌려주고 다른 객체의 필드인 객체는 `BinarySerializer::deserializeNested` 로 읽습니다. 맨 바깥 `deserialize` 는 bool 그대로입니다(두 실패를 가를 쓸 곳이 없고 부르는 곳이 50 곳 넘음).
- 기록 타입이 다른 필드의 이관은 프로퍼티 단위(`tryCoercePropertyBinary` · `applyPropertyTextCoerced`, `SchemaMigrate.h`)로 하고 비트필드에는 그 비트만 씁니다. 스키마 이관 계층(점 경로 · orphan 적용 · `movePropertyValue`)은 2026-10-23 첫 출시 전 규칙으로 지웠습니다.
- 근본 원인은 `PropertyInfo::getRawPtr` 가 비트필드에도 그 바이트 주소를 줘 같은 바이트의 다른 플래그를 덮는 것이었습니다. 이제 비트필드는 nullptr 이고(`[[nodiscard]]`) `getValue` · `setValue` 로만 읽고 씁니다(2026-10-22, 부르는 곳 전부를 읽어 확인; 걸러지지 않던 `ComponentDefaults::applyPatch` 는 `applyPropertyTextCoerced` 로). 비트필드 여부는 생성된 `PropertyInfo` 데이터라 컴파일 때 막지 못하고 값 주소를 내주는 함수 하나가 거절하는 데까지가 형식으로 막은 범위입니다. `PropertyInfo` 의 `_offset` · `_bitMask` 는 비공개이고 읽기는 `getOffset` · `getBitMask`, 비트 쓰기는 `writeBitFieldSilently` · `setValue` 로만 합니다.

## 2. 단위 (모두 들어감, 병합 때 빌드 · 실행)

| 단위 | 내용 | 선행 |
|---|---|---|
| S1 | 바이너리 태그 스트림: 읽기 전 값을 담고 실패하면 되돌린다. 프로퍼티 하나를 쓰는 세 갈래(비트필드 · 컨테이너 · 값)를 `SerializerUtil::writePropertyBinary` 하나로 모았다(`UnreadableFieldKeepsItsValueBeforeTheRead`) | 없음 |
| S2 | 텍스트 형식(JSON `readObject` · XML `readXMLIntoInstance`)의 소프트 읽기도 읽기 전 값으로 되돌린다(`PropertyReadRollback`, `JSONSoftReadKeep…` · `XMLSoftReadKeep…`) | S1 |
| S3 | 원소 · 구조체 읽기 결과가 필드 실패와 객체 실패를 가른다(`FieldReadResult`, `ContainerReadResult::ObjectRejected`). 컨테이너 원소 · 컴포넌트 안의 금지된 필드가 객체째 실패한다(`ReflectionPurposeTest.RuntimeStateInAContainerElementRejectsTheSoftRead`) | S2 |
| S4 | 바이너리도 필드 실패와 객체 실패를 가른다. 기록 타입이 다른 필드의 이관을 프로퍼티 단위로 옮겨 비트필드가 다른 플래그를 덮지 않는다(`…RejectsTheBinarySoftRead` · `CoercedBitFieldReadTouchesOnlyItsOwnBit` · `OrphanAppliedToBitFieldTouchesOnlyItsOwnBit`) | S3 |

## 3. 확인

- S1 ~ S4 는 main 에서 Debug 로 빌드해 `ReflectionTest` 267 개 · `ObjectStateRoundTripTest` 등이 통과했고(2026-10-22), 2026-10-23 main(`9c4179738`) `full-build`(Debug, 에디터 포함, 경고 0)에서 `Reflection*` 245 개 · `EngineTest` 전체 3958 개 · `EditorTest` 474 개 · `SmokeTest` 71 개가 통과했다(GPU 호스트 스위트 제외).
- 확인 대기: 매니페스트 id `serializer-scene-load`(씬을 여는 기존 시나리오 하나로 바이너리 씬 로드와 XML 씬 · 프리팹 로드를 보고 `ConfigManager::readConfigJSON` JSON 소프트도 지난다).
- 바뀐 동작(요약): 읽지 못한 원소가 있는 컨테이너는 읽힌 원소만 남던 것이 읽기 전 값이 된다. 읽다 만 소유 포인터 목록 · 구조체 칸 · 컨테이너 원소 안의 금지된 필드 · 값을 담지 못하는 필드는 칸 실패로 삼켜지던 것이 객체째 실패(소프트 `Rejected`, 엄격 false)가 된다. 맨 바깥 소프트 읽기(`deserializeSoft`)도 `NotParsed` 대신 `Rejected` 이고 스트림이 깨진 것만 `NotParsed` 다. 엄격 읽기의 결과는 그대로이고, 바이너리 파일은 금지된 필드를 쓰지 않아 저작 목적 바이너리를 옛 데이터로 읽을 때만 탄다. 기존 컴포넌트 기본값 데이터(`gamesettings.xml` 11개)에는 비트필드 속성이 없어 바뀌는 값이 없다.
- 오브젝트 상태 읽기가 필드마다 한 번 더 쓰므로 씬 로드 · 플레이 스냅샷 복원 시간이 늘 수 있다. 병합 뒤 측정해 회귀가 보이면 담는 대상을 1절 끝 조건으로 좁힌다.
