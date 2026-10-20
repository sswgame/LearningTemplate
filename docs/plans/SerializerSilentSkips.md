# 직렬화가 조용히 넘기는 곳 (2026-10-21)

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: C-66 · 진행: 3/3

읽기 · 쓰기가 조용히 넘기면 데이터 결함이 숨습니다. 페이로드를 끝까지 읽었는지는 태그 스트림 두 경로 · diff · 값 복사가 `SerializerUtil::applyPropertyBinary` 하나로 보고, B 코덱 열거형 폭은 이미 열거자 범위입니다(2026-10-20).
남은 결함은 **읽기가 실패한 필드에 앞부분이 써진 채 남는 것**입니다. 컨테이너 · 구조체는 원소와 필드를 채운 뒤 끝 검사에서 실패합니다. 엄격 읽기는 객체째 실패라 드러나지 않고, 읽지 못한 필드를 넘기고 이어 읽는 읽기에서만 그 필드가 반쯤 읽힌 값이 됩니다.

## 1. 정한 것

- 읽지 못한 필드를 넘기고 이어 읽는 읽기는 그 필드를 **읽기 전 값**으로 남깁니다. 읽다 만 값은 받지 않습니다.
- 값을 임시에 읽고 성공할 때만 옮기는 형식(타입을 지운 임시 생성)은 쓰지 않았습니다. `PropertyInfo` 에는 값 타입의 생성 · 이동 연산이 없어 코드 생성기와 모든 생성 코드를 바꿔야 합니다. 대신 읽기 전에 값을 이 빌드의 바이트로 담아 두고(`SerializerUtil::writePropertyBinary`, `applyPropertyBinary` 의 짝) 실패하면 되읽어 되돌립니다. 같은 보장을 직렬화 파일 안에서 줍니다.
- 담는 비용은 실패해도 이어 읽는 읽기(소프트, `skipsUnreadableProperties` 를 켠 엄격 = 오브젝트 상태)만 냅니다. 실패하면 객체째 버리는 엄격 읽기는 담지 않습니다.
- 소유 포인터 목록은 담지 않습니다. 되돌리면 팩토리가 컴포넌트를 새로 만들어 다른 객체가 됩니다. 읽다 만 소유 포인터 목록은 객체째 실패로 멈춥니다(예전에는 반쯤 찬 목록으로 이어 읽었습니다).
- 값을 담지 못하거나 되돌리지 못하면(코덱 결함) 오류를 남기고 객체째 실패합니다. 기본값으로 넘어가지 않습니다.
- 담기와 되돌리기는 `PropertyReadRollback`(`SerializerUtil.h`) 하나로 세 형식(바이너리 태그 스트림, JSON `readObject`, XML `readXMLIntoInstance`)이 함께 씁니다. 소유 포인터 목록을 담지 않는 규칙과 오류도 거기 한 곳에 있습니다.
- 되돌리는 단위는 **실패를 받은 필드 하나**입니다. 구조체 칸은 안쪽 읽기가 같은 규칙으로 안쪽 필드만 되돌리므로 칸 자신은 담지 않습니다(읽힌 안쪽 필드를 버리지 않습니다).
  컨테이너는 원소 단위 orphan 이 아니라 칸째 되돌립니다. 원소 하나만 빼고 나머지를 받으면 바이너리와 다른 값이 되고, 다음 저장이 그 원소를 조용히 지웁니다.
- XML 은 칸째 되돌리면 읽힌 원소도 버려지므로, 그 칸을 읽는 동안 쌓인 원소 orphan 을 지우고 **칸 원문 하나**를 orphan 으로 남깁니다. 바이너리와 JSON 이 칸 페이로드 하나를 orphan 하나로 남기는 것과 같습니다.
- 읽기 결과는 **필드 실패와 객체 실패를 형식으로 가릅니다**(S3). 필드 · 구조체 읽기는 `FieldReadResult`(Read · FieldFailed · ObjectRejected), 원소 읽기는 `ContainerReadResult` 에 `ObjectRejected` 를 더했습니다(`ContainerVisitor.h`).
  예전에는 둘 다 `bool` 의 false 여서 안쪽의 금지된 필드가 바깥에서 칸 실패로 낮아졌습니다. 받는 쪽은 `default` 없는 `switch` 로 세 값을 다 다루므로 값이 늘면 빌드가 멈춥니다.
  필드의 끝은 형식이 알므로(XML 노드, JSON 값, 바이너리 페이로드 크기) 필드 경계에서 `StreamBroken` 은 그 필드의 실패입니다(`ContainerVisitor::toFieldResult`).
- 담는 대상은 좁히지 않았습니다(S3 에서 따짐). 스칼라 처리기가 반쯤 쓴다는 반례가 있습니다 — 내장 벡터 글 처리기(`registerVectorTextHandler`)는 앞 축을 쓴 뒤 다음 축에서 실패합니다.
  처리기 등록이 "성공할 때만 쓴다"를 말하지 않으므로 타입으로 가를 근거가 없습니다. 좁히려면 등록 API 가 그 약속을 갖거나 처리기가 임시에 읽고 옮기게 바꿔야 합니다(측정에서 회귀가 보일 때).

## 2. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| S1 | 바이너리 태그 스트림(`BinarySerializer` 의 `deserializeTagged`): 읽기 전 값을 담고 실패하면 되돌린다. 프로퍼티 하나를 쓰는 세 갈래(비트필드 · 컨테이너 · 값)를 `SerializerUtil::writePropertyBinary` 하나로 모아 태그 스트림 쓰기 · 값 복사 · 되돌리기가 함께 쓴다 — **들어감**(`ReflectionSerializationTest.UnreadableFieldKeepsItsValueBeforeTheRead`, 빌드 · 실행은 병합 때) | 없음 |
| S2 | 텍스트 형식의 소프트 읽기: JSON `readObject` 와 XML `readXMLIntoInstance` 도 읽지 못한 필드를 읽기 전 값으로 되돌린다. 담기 · 되돌리기를 `PropertyReadRollback` 으로 모아 바이너리와 함께 쓴다(1절의 단위 규칙) — **들어감**(`ReflectionSerializationTest.JSONSoftReadKeepsUnreadableFieldValueBeforeTheRead` · `XMLSoftReadKeepsUnreadableFieldValueBeforeTheRead`, 빌드 · 실행은 병합 때). 동작 변경은 3절 | S1 |
| S3 | 원소 · 구조체 읽기 결과가 필드 실패와 객체 실패를 가른다: `FieldReadResult` 를 두고 `ContainerReadResult` 에 `ObjectRejected` 를 더한다. XML `readXMLIntoInstance` · JSON `readObject` · `readProperty` · `readJSONValue` 가 그것을 돌려주고, 컨테이너 원소인 구조체 · 컴포넌트 안의 금지된 필드가 칸 실패로 낮아지지 않고 객체째 실패한다(1절) — **들어감**(`ReflectionPurposeTest.RuntimeStateInAContainerElementRejectsTheSoftRead`, 빌드 · 실행은 병합 때). 동작 변경은 3절 | S2 |

## 3. 확인

- S1 · S2 · S3 은 빌드하지 않았습니다. 병합 때 `ReflectionTest` 의 `ReflectionSerializationTest.*` · `ReflectionPurposeTest.*` 와 오브젝트 상태 테스트(`EngineTest` 의 `ObjectStateRoundTripTest.*`)를 돌리고, 씬을 여는 기존 시나리오 하나로 바이너리 씬 로드와 XML 씬 · 프리팹 로드를 봅니다. 설정 읽기(`ConfigManager::readConfigJSON`, JSON 소프트)도 지납니다.
- S2 로 바뀌는 동작(텍스트 형식 소프트 읽기):
  - 읽지 못한 원소가 있는 컨테이너는 읽힌 원소만 남던 것이 이제 읽기 전 값입니다. XML 은 그 칸의 orphan 이 실패한 원소 글들에서 칸 원문 하나로 바뀝니다.
  - 읽다 만 소유 포인터 목록(컴포넌트 목록에서 원소가 금지된 필드 · 범위 밖 값으로 실패)은 반쯤 찬 목록으로 이어 읽던 것이 이제 객체째 실패(`Rejected`)입니다.
  - XML 구조체 칸 안의 금지된 필드는 소프트 읽기에서 칸 실패로 삼켜지고 성공했던 것이 이제 객체째 실패입니다(루트의 금지된 필드와 같습니다).
  - 값을 담지 못하는 필드(세 형식이 실어 나를 수 없는 타입)는 텍스트 소프트 읽기에서도 객체째 실패입니다. 모든 PROPERTY 를 훑는 테스트가 그런 타입을 막고 있어 실제로 걸리는 필드는 없다고 짐작합니다(확인 안 함).
- S3 으로 바뀌는 동작(XML · JSON):
  - 컨테이너 원소인 값 구조체 · 소유 포인터 컴포넌트 안의 금지된 필드는 칸째 되돌려 orphan 으로 넘기던 것이 이제 객체째 실패입니다(소프트 `Rejected`, 엄격 false). 원소를 더 읽지 않고 멈춥니다.
  - JSON 구조체 칸(컨테이너가 아닌 필드) 안의 금지된 필드도 같습니다. 예전에는 그 칸이 orphan 으로 넘어갔습니다(XML 은 S2 에서 이미 객체째 실패).
  - 엄격 읽기의 결과(false)는 그대로입니다. XML 엄격 읽기는 안쪽 객체 실패를 만나면 남은 필드를 더 읽지 않습니다(결과는 같고 오류 줄이 줄 수 있습니다).
- 바이너리는 아직 가르지 못합니다. 원소 · 구조체 칸은 `BinarySerializer::deserialize`(엄격, `bool`)로 읽혀 안쪽 금지된 필드가 소프트 · 오브젝트 상태 읽기에서 칸 실패(되돌림 · orphan)가 됩니다.
  바이너리 파일은 금지된 필드를 쓰지 않으므로 이 경로는 저작 목적 바이너리를 옛 데이터로 읽을 때만 탑니다. 닫으려면 `deserializeTagged` · `deserializeValueBinary` · `applyPropertyBinary` 가 `FieldReadResult` 를 돌려줘야 합니다(공개 API, 호출처 다수 — 다음 단위 후보).
- 오브젝트 상태 읽기가 필드마다 한 번 더 쓰므로 씬 로드 · 플레이 스냅샷 복원 시간이 늘 수 있습니다. 병합 뒤 측정해 회귀가 보이면 담는 대상을 좁힙니다. 좁히는 조건은 1절 끝(처리기가 성공할 때만 쓴다는 약속)입니다.
