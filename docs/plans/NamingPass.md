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

### 남은 것
- `MathUtil::getRandom` 은 중복 정리 계획의 "결정적 난수를 Core 로" 단계와 함께 옮긴다.
- 사용자 결정(2026-10-10): `PhysicsWorld` 는 그대로 둔다. 나머지는 상용 엔진과 견주어 정했다 — `GameConfig` · `GameSettings` 는 그대로(언리얼도 프로젝트 설정과 게임 설정이 따로), `GameplayAbilityConfig` · `GameplayAbilityDef` 는 그대로(정의가 설정을 품는 층), `ResourceUtil` 은 `ResourcePaths` · `ResourceIO` 로 나누기(언리얼 `FPaths` / `FFileHelper`), `EditorUtil` 은 경로를 `EditorPaths` 로 빼기, `FrameRendererUtil` 은 그대로(언리얼 `RenderUtils`). 결정은 [결정 기록](../09_Decisions.md) 5-4.

## 점검이 보지 못한 곳
`.cpp` 지역 변수 · 람다, `Games/` · `Server/` · `Test/` · `Tools/` · HLSL · Python 스크립트는 거의 보지 않았다. 코드모드 사전 실행 때 같은 기준으로 이 범위의 식별자를 한 번 더 훑는다.
