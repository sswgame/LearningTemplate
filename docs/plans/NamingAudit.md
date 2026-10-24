# 다음 이름 점검

> 상태: 끝 · 선행: 없음 · 영역: project · 옛번호: C-17 · 진행: 4/4

2026-10-13 이름 점검에서 남긴 네 가지다. 기준은 [docs/agents/NamingCpp.md](../agents/NamingCpp.md) "Class suffixes" 절과 "Type suffixes" 항목(`Settings` · `Config` · `Def` · `Context` · `Unit`)과 [결정 기록](../09_Decisions.md) 5-1 이다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| N1 | `Context` 접미어 타입이 무엇을 담는지 점검한다 | 끝남(2026-10-24, 아래 결과) |
| N2 | `*Util` 정적 클래스 중 한 타입의 메서드가 된 것이 나은 것을 고른다 | 끝남(2026-10-24, 아래 결과) |
| N3 | `Unit` 계열이 무엇을 가리키는지 가른다 | 끝남(2026-10-24, 아래 결과) |
| N4 | 단독 `value` 지역 · 멤버 변수를 뜻이 드러나는 이름으로 | 끝남(2026-10-24, 아래 결과) |

## 2. 결과

**N1.** 소스의 `*Context` 타입 약 50 개(주석 · 테스트의 보조 구조체 제외)를 모았다. 거의 모두 "호출 하나 · 패스 하나 · 프레임 하나 동안 호출자가 피호출자에게 넘기는 환경"(`RenderGraphPassContext` · `UIPaintContext` · `SerializeContext` · `BehaviorContext` · `ObjectLoadContext` 등)이거나 그래픽스 API 의 용어(`RHICommandContext` · `*PlatformContext`)라 그대로 둔다. 고친 것은 하나다.

- `LiveReloadManager::ModuleContext` → `ModuleRecord`. 등록된 모듈 하나의 상태(경로 · 핸들 · 리로드 예약)를 매니저가 들고 있는 기록이지 호출에 넘기는 환경이 아니다. 같은 파일의 `getOrCreateContext` → `getOrCreateRecord`, `clearContexts` → `clearRecords` 도 함께 바꿨다(호출처는 `LiveReloadManager.{h,cpp}` 안뿐이다).
- 그대로 둔 의심 둘: `TelemetryContext`(스풀 첫 줄에 적는 실행 속성 — 이름의 뜻이 "이 실행의 문맥"이라 맞다), `GameplayEffectContext`(언리얼 GAS 용어).
- `NamingCpp.md` 에 `Context` · `Record` 의 뜻을 한 항목으로 적었다.

**N2.** `*Util` 클래스 121 개 중 같은 이름의 타입이 따로 있는 것은 일곱이다(`EditorViewTargetUtil` · `FrameRendererUtil` · `InspectorBuiltinValueUtil` · `MeshModifierUtil` · `ReflectUnitUtil` · `SceneNavigationUtil` · `ServiceRecordUtil`). 모두 그 타입의 인스턴스를 받는 함수가 아니라 기능 하나에 딸린 상태 없는 도우미(정적 함수 · 상수)라 메서드로 옮기면 헤더 의존만 늘어 그대로 둔다. 나머지 114 개는 짝이 되는 타입이 없는 순수 도우미 모음(`StringUtil` · `HashUtil` 등)이다.

**N3.** `Unit` 은 세 뜻이 이름 공간으로 갈려 있다: 게임 플레이 유닛(`RTSUnit` · `SRPGUnit` · `ArenaUnitComponent` · `ConquestUnit`), `AnimationSystem` 이 평가하는 스켈레탈 메시 컴포넌트(`registerUnit` · `getUnits` · `onAnimationUnitDetached`, 헤더 머리 주석이 "애니메이션 유닛(`SkeletalMeshComponent`)"이라 정의한다), 측정 단위(`ReflectUnit` · `stepsPerUnit`). 서로 같은 파일에서 섞이지 않아 이름은 그대로 둔다. `NamingCpp.md` 에 세 뜻을 적었다.

**N4.** 단독 `value` 는 지역 · 멤버 약 650 곳이었다. 대부분은 이름이 곧 뜻이다: 비트 · 해시 · 엔디언 도우미의 인자, `Wrapper<T>::_value`, `T value{}; return value;` 꼴의 읽기 함수의 반환 칸, `key=value` 를 쪼갠 오른쪽. 이런 곳은 그대로 둔다. 뜻이 다른 이름으로 드러나는 열세 곳을 고쳤다: `AudioMixer.cpp`(`scaledSample`) · `AudioClip.cpp` 셋 · `AudioStream.cpp`(`decodedSample`) · `TerrainBrush.cpp`(`holeFlag`) · `MeshComponent.cpp`(`firstPersonFlag`) · `SkeletalMeshComponent.cpp`(`shareFlag`) · `TileMapXML.cpp` 둘(`paletteNumber`) · `TileGridUtil.cpp` 둘(`brushNumber`) · `UIPreviewPanel.cpp`(`scrubSeconds`). 이름을 바꾼 범위는 한 함수 안이다(리다이렉트가 필요한 공개 이름 아님).
