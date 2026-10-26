# 리플렉션 직렬화 형식 계획 (2026-10-17)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 표

같은 `PROPERTY` 선언 하나에서 쓰임새가 다른 두 바이너리 형식을 만듭니다. 오래 남는 데이터는 **자기 기술형**(A), 같은 빌드 안에서만 오가는 데이터는 **고정 배치형**(B)입니다. B 의 읽기 · 쓰기 · 비교는 빌드 시간에 코드 생성기가 타입마다 만듭니다. 사용자는 B 를 선언하지 않고 `REFLECT()` · `PROPERTY()`(`Min` · `Max` · `Step` · `MaxCount`)만 씁니다. 마이그레이션은 A 가 맡고 B 는 확장 지점만 남깁니다.
**명세 · 호환 정책 · 테스트의 정본은 [`Source/Engine/Serialization/Fixed/README.md`](../../Source/Engine/Serialization/Fixed/README.md)**(2 · 3 · 5 · 6절)이고 호출 계약은 `FixedLayout.h` 머리 주석입니다. 이 문서는 단계 기록(9절)과 캐스트 비용 판단(7절)만 남깁니다. 결정은 [결정 기록](../09_Decisions.md) 5-4 · 5-9.

## 1. 구현 위치

단계 1 ~ 6 · 7-a · 7-b 는 들어갔습니다(9절). 런타임 `Source/Engine/Serialization/Fixed/FixedLayout.*`, 생성기 `Tools/ReflectionParser/FixedCodecGenerator.*`(필드 분류 `AstVisitor.cpp`, 키 `AnnotationFields.cpp`), 테스트 `Test/ReflectionTest/TestFixedLayout.cpp`. 온라인 해시 · 거절은 `OnlineProtocolRegistry.*` · `OnlineReplyUtil::markError`, 범위 읽기 공통 끝은 `SerializerUtil::isWithinDeclaredRanges`.
**근본 원인(끝남).** 바이트 배치의 원본이 선언과 따로 손 코드에 있어 필드를 더하고 손 버전 상수를 안 높이면 조용히 잘못 읽었습니다. 지금은 선언에서 생성한 코덱의 스키마 해시가 잡습니다. 컴팩트 모드와 호출부 없는 `cloneObject` 는 지웠고 `writeInt` 의 범위 자르기는 쓰기 실패로, 모르는 타입 건너뛰기는 읽기 실패로 바꿨습니다(옛 `A-13` · `A-14`).

## 2. 두 형식의 역할 · 3. 고정 배치형(B) 명세 · 4. 코드 생성 변경 범위 · 5. 호환 정책 · 6. 테스트

위 README 가 정본입니다(생성기 변경 범위는 `FixedCodecGenerator.*` · `AstVisitor.cpp` · `ParsedReflection.h`).

## 7. 리플렉션 캐스트 비용

- `castTo` 는 업캐스트면 컴파일 시간에 끝나고, 다운캐스트는 대상 타입 조회(`TypeLookupCache`) + 동적 타입(`Component` 는 `_pTypeInfo` 보관, `GameObject` 는 가상 `getTypeInfo()`) + 상속 검사(`TypeInfo::isDerivedFrom`, 조상 디스플레이 테이블, 잠금 · 문자열 비교 없는 O(1))입니다. `castTo<Component>( Component*, 타입 )` 은 항상 성공하므로 종류로 거를 때는 `isDerivedFrom` 을 씁니다.
- **DFS 구간 번호 채택 안 함**: 디스플레이도 O(1) 이고 구간 번호는 타입 추가(DLL 로드, 핫 리로드)마다 번호가 바뀌며 생성기가 모듈 하나만 봐 전역 번호를 정할 수 없다.
- `GameObject::getTypeInfo` 결과 캐시는 이득이 측정되지 않고 엔진 재생성에서 낡아 넣지 않았다(결정 기록 5-4).
- 남은 비용의 원인은 타입 없는 핸들과 씬 전체 순회였고 `TypedComponents`(끝나서 문서를 지웠다 — git log)가 타입별 목록(7-c)과 `TComponentHandle<T>`(7-d)로 풀었다. 종류별 배열 저장소는 범위 밖 후보다.
- 둘째 · 가상 베이스를 거친 다운캐스트는 포인터 보정이 없어 잘못된 주소가 되므로 생성기가 `REFLECT` 타입의 둘째 · 가상 베이스를 생성 오류로 막는다(7-b). `_typeID` 는 등록 순서 카운터라 네트워크 · 디스크에 싣지 않는다.

## 8. 위험과 남은 확인

위험(적격 아닌 타입의 기동 시간 발견, 의도치 않은 와이어 변경, 의미만 바뀐 변경에 `kBaseVersion` · 손 버전 상수 올리기)은 README 의 위험 절에 있습니다. 남은 확인은 없습니다. `Min` · `Max` 의 상수 이름 도입(`wt/minmaxconst`)과 숫자 `MaxCount` 의 상수 이름 전환(`wt/onlinemax` · `wt/maxkits`)은 끝났고, 문서 생성기(`ConfigReference.py`)는 상수를 같은 파일에서만 풀어 다른 헤더의 상수는 그 사실을 밝히며 멈춥니다(현재 `PhysicsSettings` 뿐).

## 9. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | 생성기 · `TypeInfo::_pFixedCodec` · `FixedLayout` 런타임 · 테스트 | 끝남(2026-10-17, `a01e7a79a`) |
| 2 | `GimmickCircuitState` 에서 컴포넌트는 A, 묶음 · 넷 조각은 B | 끝남(`e6ae53938`, 2026-10-20 Debug `full-build` 경고 0 · 기믹 45 케이스 · `ReflectionTest` · `-L nogpu` 엔진 5 샤드 통과) |
| 3 | `FightingMatch` 롤백 B · 세이브 A, 두 읽기가 같은 `validateState`. 손 코덱 · 손 버전 상수 · `RoundSeries` · `InputCommandBuffer` 의 손 `writeState` · `readState` 삭제 | 끝남(`88add323a`, `5a14ccb39`) |
| 4-a | Mech · Horror 스냅샷을 B 로(`PROPERTY` 범위 키만, 손 코덱 · `kMagic` 없음). 넷 채널 등록 · 해시 섞기는 후보 `NetChannelSchemaHashHandshake` | 끝남(`7fa9a9c70`, `016656bc2`) |
| 4-b | 인스펙터가 필드 이름 형태 `Min` · `Max` 를 씀(3.8) | 끝남(`7ce9eafe2`) |
| 5-a | Hello 첫 필드 스키마 해시, `OnlineProtocolRegistry`, "업데이트 필요" 거절 뒤 재접속 없음(5절). 키트 결과 열거형은 모두 `UpdateRequired` 를 갖고 `OnlineReplyUtil::markError` 가 키트 표보다 먼저 `kVersionMismatch` 를 그 값으로 바꿈 | 끝남(`5a1305dc3`) |
| 5-b | 온라인 11 키트를 B 로(`XxxProtocol::MessageTypes`, 손 코덱 · 손 `kVersion` 삭제, 영속 값은 `ServiceRecordUtil` 의 A). 결정 기록 5-9 | 끝남(`78b5affa3`) |
| 6 | 컴팩트 모드와 호출부 없는 `cloneObject` 삭제, `A-13` · `A-14` 해결 | 끝남(`626210330`) |
| 7-a | 열거형 이름 조회를 `EnumInfo::_listDenseName` · `EnumNameCache` 로(3.13) | 끝남(`58a651537`) |
| 7-b | `REFLECT` 둘째 · 가상 베이스를 생성 오류로(7.4) | 끝남(`58a651537`) |
| 7-c | 매니저의 타입별 컴포넌트 목록 | 끝남(2026-10-19, `TypedComponents`) |
| 7-d | 타입 핸들 `TComponentHandle<T>` | 끝남(2026-10-19, `TypedComponents`) |
| 8 | `MaxCount` 가 C++ 정수 상수 이름을 받고 `PropertyMetadata::_maxCount` 에 상한을 둠(3.5). 기믹 · 온라인 키트의 숫자 중복 117 곳을 상수 이름으로, 대조 테스트 삭제 | 구현 끝(2026-10-21, `wt/maxcount`, 빌드 · 테스트 미확인) |

단계 3 의 기반 헤더 셋(`FrameData.h` · `InputCommandBuffer.h` · `RoundSeries.h`)을 건드리는 다른 작업과 병합할 때 겹칠 수 있습니다. 세이브 읽기의 `Min` · `Max` 는 자기 기술형 세 포맷의 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 시퀀스 원소까지 봅니다(범위 범위를 고르는 인자 없음 — 설정 JSON · 데이터 XML · 세이브가 같은 계약). 단계 4-a: Horror 발전기의 진행 · 표시 두 목록은 원소 하나(`HorrorGeneratorSnapshot`)로 합쳤고, 메시지는 스트림 머리 해시를 그대로 실어 배치가 다른 빌드의 바이트를 `SchemaMismatch` 로 거절합니다.
