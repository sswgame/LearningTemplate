# Games (게임 프로젝트 관리)

엔진을 기반으로 실제 개발할 게임 컨텐츠(팩)들이 모여있는 폴더입니다.
어떤 게임을 활성화하여 빌드할지는 CMake 설정인 `SW_ACTIVE_GAME` 변수를 통해 결정합니다. 기본 템플릿은 `Empty`입니다 (`-DSW_ACTIVE_GAME=Empty`).

이때 컴파일되는 실행 파일과 타겟의 이름은 어떤 게임을 선택하든 항상 **SWGame**으로 고정됩니다. 이는 런타임에 게임 로직을 갈아끼우는 핫리로드(LiveReload) 기능이 고정된 모듈 이름(`kGameModuleName`)을 안정적으로 찾을 수 있도록 하기 위함입니다.

개발 중 모듈 핫리로드가 동작할 때 대상이 되는 모듈들의 이름은 기본적으로 다음과 같습니다 (필요시 전역 변수를 오버라이드할 수 있습니다):

- `kGameFrameworkModuleName` — 뼈대가 되는 프레임워크 (기본값: `GameFramework`)
- `kGameKitModules` — 컴마(,)로 구분된 키트 목록 (기본적으로 Overworld, TurnBattle, ActionCombat 키트가 포함됩니다. 의존성은 `:dep|dep` 형태로 지정 가능)
- `kGameModuleName` — 실제 활성화된 게임 모듈 (기본값: `SWGame`)

## 새로운 게임 추가하는 방법 (초보자 가이드)

새로운 게임 프로젝트를 시작하려면 아래 5단계를 따라주세요:

1. **템플릿 복사하기**: `Source/Games/Empty/` 폴더를 복사하거나 새로운 게임 폴더 `Source/Games/MyGame/` (원하는 게임 이름)를 생성하세요.
2. **필요한 키트 연결하기**: `MyGame/CMakeLists.txt` 파일을 열고, 내 게임에 필요한 키트(예: ActionCombat 등)를 `target_link_libraries`에 추가해 주세요.
3. **게임 리소스 폴더 만들기**: 3D 모델, 텍스처, 셰이더 등 게임 에셋을 담을 폴더를 `Resource/game/mygame/` 경로에 만들어 주세요. 그리고 C++ 코드(`configureBootstrap` 함수 내부)에서 이 폴더를 루트 팩으로 지정해야 합니다.
4. **CMake 활성화**: 빌드 시 내 게임을 타겟으로 잡기 위해 CMake를 설정할 때 `-DSW_ACTIVE_GAME=MyGame` 옵션을 넣어줍니다.
5. **키트(Kit)가 변경되었다면**: 만약 2번 단계에서 새로운 키트를 추가했다면, 핫리로드가 해당 키트도 다시 불러올 수 있도록 C++ 상의 `kGameKitModules` 목록을 업데이트하거나 CMake의 DELAYLOAD 목록에 추가해 주어야 완벽하게 동작합니다.
