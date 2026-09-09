# Engine (핵심 엔진 모듈)

오브젝트(Object), 그래픽스 렌더링(RHI), 리플렉션(Reflection), 씬(Scene) 관리 등 **진짜 게임 엔진의 코어 로직**이 모여있는 곳입니다.

Foundation(로그/파일/문자열 등)은 `Source/Core`의 `Core_objects`에서 한 번만 컴파일되며, Engine이 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 export합니다.

## 내부 티어 (허용 의존 방향)

물리적으로 SHARED를 쪼개지 않은 상태이므로, 폴더 간 include 방향으로만 순환을 가둡니다.
아래 표는 **include 그래프를 Tarjan SCC 로 줄여 위상 정렬해 얻은 것**입니다. 손으로 고른
순서가 아니므로, 코드가 바뀌면 표도 다시 계산해야 합니다.

| 티어 | 폴더 | 뜻 |
|---|---|---|
| 0 | `Common` · `Utility` | 토대. Engine 의 어느 것도 참조하지 않는다. |
| 1 | `Animation` · `Audio` · `Localization` · `Physics` · `Spatial` | 코어가 쓰는 잎 서브시스템. 코어를 거꾸로 참조하지 않는다. |
| 2 | `Config` · `Graphics` · `Module` · `Object` · `Reflection` · `Resource` · `Scene` · `Sequencer` · `Serialization` · `Window` | **코어 묶음 — 강결합이다. 내부 순서는 없다.** |
| 3 | `Dialogue` · `Input` | 코어 위에 올라가는 것. |
| 4 | `EngineLoop` 등 루트 파일 | 전부를 엮는 자리. |

**티어 2 는 하나의 강결합 묶음입니다.** 열 폴더가 서로 도달 가능합니다 — 씬이 에셋을 읽고,
컴포넌트가 머티리얼을 들고, 리플렉션이 직렬화를 부르고, 핫리로드가 씬의 TypeInfo 를 다시
묶습니다. 그러니 "Reflection 이 Object 보다 아래" 같은 **내부 순서를 주장하지 않습니다.**
거짓인 순서를 문서에 적어 두는 것보다, 참인 경계(티어 간 방향)를 검사하는 편이 낫습니다.
묶음을 풀어내는 일은 [docs/06_Backlog.md](../../docs/06_Backlog.md) 에 측정된 엣지 수와 함께
적혀 있습니다.

티어가 아닌 것이 둘 있습니다. 검사도 이 둘을 예외로 둡니다.

- **prelude·경로 헬퍼**: `EngineMinimal.h`, `Common/Common.h`, `Resource/ResourceUtil.h`.
  타입 별칭·전방 선언 우산 헤더와 리소스 경로 해석 헬퍼라 어느 티어에서든 쓸 수 있습니다.
- **배선 파일**: `Common/EngineServices.cpp`, `Reflection/ReflectGenerated.h`,
  `Resource/ResourceManager.cpp`. 노출하는 모든 서브시스템을 알아야 하는 자리입니다.

금지 include 자동화: `py -3 Scripts/lint/CheckEngineLayers.py`
(Engine → `Editor/` / `GameFramework/` / `Games/` 금지 + 위 티어 방향. **위반은 실패입니다** —
예전에는 손으로 고른 네 쌍만 경고로 찍고 실패시키지 않아서, 쌓여도 아무도 몰랐습니다.)

후속(별 PR): `EngineRHI` / `EngineReflection` 물리 분할 — 이 저장소 로드맵에서는 설계만.

## 주요 시스템 디렉터리 구조
- **Object/**: GameObject · Component · Prefab. 틱/구조 동결·사용법은 [Object/README.md](Object/README.md)
- **Scene/**: Scene · SceneManager · 2D/3D 공간 분할 가속 구조체 ([Scene/README.md](Scene/README.md))
- **Reflection/**: 매크로 · TypeRegistry · Builtins. [Reflection/README.md](Reflection/README.md) · 생성기 [ReflectionParser](../../Tools/ReflectionParser/README.md)
- **Graphics/**: RHI · Material · Shader · FrameRenderer. [Graphics/README.md](Graphics/README.md)
- **Input/**: InputManager · ActionMap · 장치(Keyboard/Mouse/Gamepad) 추상화. [Input/README.md](Input/README.md)
- **Resource/**: AssetDatabase · ResourceManager · ResourceUtil · ResourcePackManager (VFS .pack) · AssetStreamingQueue
- **Serialization/**: 직렬화 (BinarySerializer · JsonSerializer · XmlSerializer · Archive)
- **Module/**: LiveReloadManager · ModuleTypeRegistry · ReloadFileManager. DLL 핫스왑과 그에 따른
  TypeInfo 재결합을 담당합니다. 예전에는 `Utility/Module` 에 있었지만, 모든 로드된 Scene 의
  GameObjectManager 를 다시 묶는 **상위 서브시스템**이라 `Utility`(최하위 티어)가 아닙니다.
- **Utility/**: Format (KeyValueFile), Json, Xml, CommandStack, Debug — 진짜 최하위 헬퍼만 둡니다.
- **Task 시스템**: Core의 [Task/README.md](../Core/Task/README.md) (`TaskManager` / `TaskHandle`)

## 동작 방식
- **개발 모드(Dev)**: `SHARED` (DLL) 형태로 빌드되어 동적으로 로드됩니다.
- **배포 모드(Shipping)**: 성능 최적화를 위해 `App`에 `STATIC`으로 묶입니다.

## 핵심 주의사항
`Engine` 내부에 작성된 코드는 **`EditorModule`이나 `Games` / `GameFramework` 로직에 직접 의존하면 안 됩니다.**
엔진은 플랫폼이자 뼈대이므로, 게임별로 달라지는 구체적인 로직이나 에디터 전용 UI 코드가 이 폴더를 더럽히지 않도록 주의하세요.
에디터와 통신이 필요할 때는 RuntimeAPI·공통 인터페이스나 델리게이트를 통합니다.
