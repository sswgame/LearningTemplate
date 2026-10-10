# 이름 정리 계획

이름 점검(헤더 위주, 2026-10-10)이 낸 제안을 하나씩 다시 따져서 **채택 · 문서로 해결 · 기각**으로 나눴다. 점검이 "모호하다"고 한 것이 곧 "바꾸라"는 뜻은 아니다 —
`GameObjectManager` 는 한 씬의 `GameObject` 를 만들고 소유하고 틱하는 일을 이름 그대로 하므로 그대로 둔다(사용자 지적, 2026-10-10). 약어 철자는 끝났다(약어는 대문자 — AGENTS.md "Function names").
기각된 구조 변경(`ResourceUtil` 소유 객체화 · `EditorContext` 소유 분할, `docs/09_Decisions.md`)은 이름 변경으로도 건드리지 않는다.

## 검토 결과

### 기각 (이름이 이미 맞다)
| 제안 | 이유 |
|---|---|
| `GameObjectManager` → `SceneObjects` | 하는 일을 정확히 말한다. `SceneObjects` 가 더 흐리다. 헤더 주석("씬 하나의 얼굴")만 관계를 정확히 고친다 |
| `GPUScene` → `GpuFrameData` | 언리얼의 `GPUScene`(인스턴스 데이터를 GPU 에 올려 둔 장면)와 같은 용어다 |
| `InputManager` → `InputHub` | 싱글턴 허브라는 결정(`docs/09`)과 `Manager` 접미 관례 그대로 |
| `AssetManager` → `AssetFacade` | 캐시 묶음이라도 `Manager` 로 일관(`GameObjectManager` · `ConfigManager` 와 같은 층) — 단 그 안의 `getMaterialManager` 는 아래 채택 |
| `TypeInfo` 세 뜻 → `*KindRow` | `RenderPassTypeInfo` 는 "패스 종류의 정보"를 이름 그대로 담는다 |
| `OverlapInfo` · `CollisionInfo` · `HitInfo` → `*Event`/`*Contact` | 접촉 결과 값 묶음이고 `Info` 가 맞다(언리얼 `FHitResult` 와 같은 자리) |
| `ModuleService` → `ModuleServiceTable` | C ABI 이름이라 ABI 도장 · 모듈 전부에 걸린다 |
| `ComponentRegistry` → `*Index` 등 | `Registry` 는 이 저장소에서 "등록된 것의 목록"으로 일관 |
| `WeakInternTable` → `WeakSharedTable` | `Intern` 은 `hashed_string` 과 같은 쓰임 |
| 열거형 `Auto` · `Normal` · `Custom` · `Count` | 열거형 안에서만 뜻이 서고 각자 문맥이 분명하다. `Misc = 0`(`HorrorCatalog.h`) 하나만 이름을 바꾼다 |
| 통지 동사(`notify` · `broadcast` · `publish` …) 규칙화 | 추정에 그친다 |

### 문서로 해결
어휘표(`acquire` · `tryGet` · `getOrCreate` · `ensure` · 프레임 진행 동사)와 접미사 표 · `RT` 는 AGENTS.md "Function names" 와 [코딩 규칙 예시](../04_CodingGuidelines.md) 2 절에 올렸다.

### 끝난 것 (2026-10-10 기계적 치환)
`MatrixMath::create*` → `make*`, `ctx` · `*Ctx` → `context` · `*Context`, `BlendCurveSpec` → `BlendCurveDef`, 스윕 비율 `_time` → `_hitFraction`,
부정형 불린(`_bCollideConnectedBodies` · `_bCollideJointedBodies` — 데이터도 다시 썼다 · `_bTCPNoDelay` · `_bIgnoreCost`), 동작과 어긋난 함수 넷,
`getMaterialCache` / `getTextureCache`, `RegistrationResult` 한 이름, `NetHost` 의 `nowSeconds` · `_timeoutSeconds`,
두 린트 예외 표의 "개명 예정" 59 줄(`build*` · `generateNewID` · `Attr` · 맨이름 `out`). 예외 표에는 "도메인 용어" 일곱 줄만 남았다.

### 채택 — 남은 치환 (건별 판단)
- `HorrorCatalog.h` 의 `Misc = 0`.
- 바이트 크기: 버퍼·메모리 필드만 `sizeBytes` 로(맨 `size` 354 곳은 컨테이너 크기라 대상 아님). 시간 필드는 단위 없는 것 약 66 개 가운데 초·밀리초가 모호한 것만 접미사를 붙인다 — 사전 목록을 뽑아 건별 판단.
- 파일 이름과 타입이 어긋난 곳: `EditorColor.h` 의 `Color4`, `EventType.h` 의 `IEvent`, `Crafting.h` 의 `RecipeDef` 외, `AnimNotifyHandlers.h` 의 `CameraShakeRequest` — 파일을 타입 이름에 맞춰 나누거나 옮긴다(건별).

### 채택 — 구조와 얽힌 큰 이름 (건별 결정 뒤)
- `ResourceUtil`(참조 313): 경로 해석·저장 경로 쪽과 읽기 쪽을 `ResourcePaths` / `ResourceIO` 로 이름만 나눌지. 소유 객체화는 하지 않는다. 같은 모양의 `EditorUtil`(경로 · 프리팹 스폰 · 배지 문자열), `FrameRendererUtil`(어태치먼트 이름 · 상수 · 스핀 데모 상수)은 만능 통이라 이 둘이 먼저 쉽다.
- `MathUtil::getRandom` 은 중복 정리 계획의 "결정적 난수를 Core 로" 단계와 함께 옮긴다.
- 사용자 결정(2026-10-10): `PhysicsWorld` 는 그대로 둔다. 나머지는 상용 엔진과 견주어 정했다 — `GameConfig` · `GameSettings` 는 그대로(언리얼도 프로젝트 설정과 게임 설정이 따로), `GameplayAbilityConfig` · `GameplayAbilityDef` 는 그대로(정의가 설정을 품는 층), `ResourceUtil` 은 `ResourcePaths` · `ResourceIO` 로 나누기(언리얼 `FPaths` / `FFileHelper`), `EditorUtil` 은 경로를 `EditorPaths` 로 빼기, `FrameRendererUtil` 은 그대로(언리얼 `RenderUtils`). 결정은 [결정 기록](../09_Decisions.md) 5-4.

## 순서
1. 엔진 · GameFramework 폴더 재배치와 약어 철자 통일 뒤에 기계적 치환(린트 예외 표 비우기 포함)을 약어 코드모드 틀에 얹어 한 묶음씩.
2. 구조와 얽힌 큰 이름은 건별로 사용자 결정을 받는다.

## 점검이 보지 못한 곳
`.cpp` 지역 변수 · 람다, `Games/` · `Server/` · `Test/` · `Tools/` · HLSL · Python 스크립트는 거의 보지 않았다. 코드모드 사전 실행 때 같은 기준으로 이 범위의 식별자를 한 번 더 훑는다.
