# Engine (핵심 엔진 모듈)

오브젝트(Object), 그래픽스 렌더링(RHI), 리플렉션(Reflection), 씬(Scene) 관리 등 **진짜 게임 엔진의 코어 로직**이 모여있는 곳입니다.

Foundation(로그/파일/문자열 등)은 `Source/Core`의 `Core_objects`에서 한 번만 컴파일되며, Engine이 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 export합니다.

## 내부 레이어 (허용 의존 방향)

물리적으로 SHARED를 쪼개지 않은 상태이므로, 폴더 간 include 방향으로만 순환을 가둡니다.

1. `Utility` / 플랫폼 헬퍼
2. `Reflection` (+ codegen)
3. `Object` / `Scene` / `Serialization`
4. `Graphics` (RHI, Material, RenderPass) — Reflection/Object 참조는 허용, **상위 gameplay·Editor 금지**. 상세 [Graphics/README.md](Graphics/README.md)
5. `Input` / `Window` / `Audio` / `Physics` / `Animation` — Object/Graphics에만 기대게

금지 include 자동화: `py -3 Scripts/lint/CheckEngineLayers.py`
(현재: Engine → `Editor/` / `GameFramework/` / `Games/` 금지)

후속(별 PR): `EngineRHI` / `EngineReflection` 물리 분할 — 이 저장소 로드맵에서는 설계만.

## 주요 시스템 디렉터리 구조
- **Object/**: GameObject · Component · Prefab. 틱/구조 동결·사용법은 [Object/README.md](Object/README.md)
- **Scene/**: Scene · SceneManager · 2D/3D 공간 분할 가속 구조체 ([Scene/README.md](Scene/README.md))
- **Reflection/**: 매크로 · TypeRegistry · Builtins. [Reflection/README.md](Reflection/README.md) · 생성기 [ReflectionParser](../../Tools/ReflectionParser/README.md)
- **Graphics/**: RHI · Material · Shader · FrameRenderer. [Graphics/README.md](Graphics/README.md)
- **Serialization/**: 직렬화 (포맷별 구현, 공통 코어, 객체 특화)
- **Input/**: `InputManager` + `ActionMap` (XML 레이어/트리거). 플랫폼별 구현 분리
- **Window/** · **Audio/** · **Game/** (`GameState`)
- **Physics/** · **Animation/**: 실험용 스텁 (각 폴더 README)
- **Utility/**: LiveReload, Archive, Xml, Json, Resource
- **Task 시스템**: Core의 [Task/README.md](../Core/Task/README.md) (`TaskManager` / `TaskHandle`)

## 동작 방식
- **개발 모드(Dev)**: `SHARED` (DLL) 형태로 빌드되어 동적으로 로드됩니다.
- **배포 모드(Shipping)**: 성능 최적화를 위해 `App`에 `STATIC`으로 묶입니다.

## 핵심 주의사항
`Engine` 내부에 작성된 코드는 **`EditorModule`이나 `Games` / `GameFramework` 로직에 직접 의존하면 안 됩니다.**
엔진은 플랫폼이자 뼈대이므로, 게임별로 달라지는 구체적인 로직이나 에디터 전용 UI 코드가 이 폴더를 더럽히지 않도록 주의하세요.
에디터와 통신이 필요할 때는 RuntimeAPI·공통 인터페이스나 델리게이트를 통합니다.
