# Games (게임 프로젝트 관리)

엔진을 기반으로 실제 개발할 게임 컨텐츠(팩)들이 모여있는 폴더입니다.
어떤 게임을 활성화하여 빌드할지는 CMake 설정인 `SW_ACTIVE_GAME` 변수를 통해 결정합니다. 기본 템플릿은 `Empty`입니다 (`-DSW_ACTIVE_GAME=Empty`).

이때 컴파일되는 실행 파일과 타겟의 이름은 어떤 게임을 선택하든 항상 **SWGame**으로 고정됩니다. 이는 런타임에 게임 로직을 갈아끼우는 핫리로드(LiveReload) 기능이 고정된 모듈 이름을 안정적으로 찾을 수 있도록 하기 위함입니다.

## 핫리로드 대상 모듈은 어디서 정하는가

`Config/App/AppConfig.json` 의 `_listGameKitModule` 이 정본입니다. App 이 부팅할 때 이 목록을
읽어 `LiveReloadManager` 에 키트를 등록합니다(`App::startModules` → `ModuleHost::initialize`).

```json
{
    "_listGameKitModule": [
        { "_name": "GF_Overworld",   "_listDependencyModule": [ "GameFramework" ] },
        { "_name": "GF_ActionCombat", "_listDependencyModule": [ "GameFramework", "GF_Overworld" ] }
    ]
}
```

- `_name` 은 CMake 타깃 이름과 같아야 합니다(= DLL 파일 이름).
- `_listDependencyModule` 이 비어 있으면 `GameFramework` 하나로 채웁니다.
- `GameFramework` 와 `SWGame` 은 이 목록에 적지 않습니다 — 항상 등록됩니다.
- **Shipping 은 이 파일을 읽지 않습니다.** 모든 모듈이 정적 링크라 리로드할 대상이 없습니다.

## 새로운 게임 추가하는 방법

1. **템플릿 복사하기**: `Source/Games/Empty/` 를 `Source/Games/MyGame/` 으로 복사합니다.
2. **벤치 하네스 지우기**: `BenchScene.h` / `BenchScene.cpp` 를 지우고, `EmptyGame` 의
   `_benchScene` 멤버와 그것을 쓰는 두 줄을 지웁니다. 이건 측정용이고 게임 코드가 아닙니다
   (아래 "Empty 는 왜 비어 있지 않은가" 참고).
3. **필요한 키트 연결하기**: `MyGame/CMakeLists.txt` 의 `sw_addGameModule(SWGame KITS ...)` 에
   필요한 키트를 적습니다.
4. **게임 리소스 폴더 만들기**: `Resource/game/mygame/` 을 만들고, `configureBootstrap` 에서
   `outConfig._packRoot = "game/mygame";` 로 지정합니다.
5. **CMake 활성화**: `-DSW_ACTIVE_GAME=MyGame`.
6. **키트를 추가했다면**: 3번에서 새 키트를 링크했다면 `Config/App/AppConfig.json` 의
   `_listGameKitModule` 에도 넣어야 그 키트가 핫리로드됩니다. 안 넣으면 빌드·실행은 되고
   **그 키트만 리로드되지 않습니다** — 증상이 조용하니 기억해 두세요.

## Empty 는 왜 비어 있지 않은가

`Empty` 는 템플릿이면서 동시에 **렌더 경로 측정용 벤치 하네스**를 들고 있습니다.
`-gv_benchMeshes=N` 을 주면 큐브 N 개를 격자로 세우고 매 프레임 흔듭니다.

그릴 것이 씬에 올라가야 렌더 비용을 잴 수 있고, **씬을 만드는 것은 엔진이 아니라 게임의 일**이라
여기 있습니다. `Scripts/dev/BackendSmoke.py` 와 `Engine/Graphics/README.md` 의 측정 조건이 이
플래그에 기대고 있어 타깃·플래그 이름은 바꾸지 않습니다.

그래서 파일을 나눠 두었습니다 — `EmptyGame` 은 ~40줄 템플릿이고, 벤치는 `BenchScene` 한 쌍에
전부 들어 있습니다. 새 게임을 시작할 때 지울 경계가 파일 경계와 같아야 하기 때문입니다.
