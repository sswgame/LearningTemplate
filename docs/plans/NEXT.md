# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 끝난 일은 여기 적지 않습니다. 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법과 기준값은
[검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../06_Backlog.md)(분류 A~H)에 있습니다. 작업 방식은 [작업 방식](../11_Workflow.md)이고,
병렬 작업자에게 주는 공통 규칙은 [작업자 규칙](WorkerRules.md)입니다.

## 1. 지금 상태 (2026-10-15)

main 은 `origin/main` 보다 184 커밋 앞서 있고 push 하지 않았습니다. 원격 브랜치는 `main` 하나입니다. 이번 세션에 병합한 것은 다음과 같습니다.

- 서브시스템 정리의 마지막 단계: `EngineLoop` 를 `Engine` 으로 개명, 호스트 범위(모듈 이미지 창구, 자동화 구동기), C-27 판정 마감.
- 스레드: `sw::thread` 래퍼와 `CheckStdThread` 린트, 에디터 백그라운드 작업의 결과 · 대기 판정 통합(C-24).
- 내보내기: 키트 DLL 이 `GameFramework.dll` 과 겹쳐 내보내던 심볼 제거(`SW_KIT_API` · `SW_KIT_SERVER_API`), `CheckExportMacros` 린트(C-3).
- 상수: 시간 · 바이트 단위 공용 상수와 치환 106 개 파일, `OnlineLimit.h`, `CheckWellKnownConstants` 확장. 조건 세 부분 이상은 이름을 붙이라는 `CheckConditionParts` 린트(새 줄만 검사).
- 이름: 사용자 설정의 `Applier` 를 `Handler` 로 바꿨고 한국어 "적용기" 를 없앴습니다. 정적 등록 표를 `RegistrationList` 로 모았습니다(`EditorAssetTypeActions` 포함).
- 에디터: 서버 디버그 채널 · 상태 해시 · 권한 고스트(S3 · S6 · S7), 다중 월드 단계 2~5(미리보기 씬마다 렌더러, `EditorPreviewPanel`, 모듈 ABI v6), 지형 쓰기 API(CPU), 캐릭터 편집 창(스켈레톤 · 장비 · 피팅 표시 · 프리셋 파일), 메시 모디파이어 보강, UI 혼합 배율 `MatchWidthOrHeight`.
- 결함: E-11(리로드 중복 등록), GL 체인 포커스 상실 입력 처리, 커서 클립 건너뜀 처리, 에디터 창 5 건(`visual`).
- 도구: 가드 훅 `GuardCommands.py`, graphify MCP(`.mcp.json`), `gpu-verifier` · `code-reviewer` 에이전트.

**아직 합치지 않은 작업 하나.** 작업자가 일하던 워크트리가 디스크에 있습니다. 세션이 바뀌면 에이전트는 멈추므로, 새 세션이 워크트리 상태를 보고 이어받습니다.

- `D:\w\terrain2`(`wt/terrain2`): **끝났고 병합만 남았습니다.** 커밋 셋입니다. `996645dc1`(스플랫 GPU 갱신, 청크별 충돌 바디, 메시 재생성 나눔, 식생 · 내비 연결), `68198f173`(에디터 `TerrainBrushTool` 드래그 · 되돌리기 · 저장, `camera.set`, 시나리오 `editor.terrainbrush`), `de03ac3eb`(문서).
  RHI 영역 업로드(`uploadTexture2DRegion`)는 이미 네 백엔드에 있었고 백로그의 "부분 업로드가 없다" 는 낡은 내용이었습니다. 지형에서 GPU 로 가는 길만 새로 만들었습니다.
  작업자 보고: App · EditorModule · EngineTest 경고 0, 지형 테스트 27/27, 네 백엔드에서 `editor.terrainbrush` 시나리오 PASS, DX12 · Vulkan 캡처를 직접 확인(되돌리기 뒤 칠하기 전과 같음). Debug 측정으로 5×5 쓰기 뒤 물리 틱 1416 ms → 17 ms, 257² 쓰기 프레임 288 ms → 25 ms 입니다.
  `EditorTest` 와 `hostgpu` 는 돌리지 않았습니다. 병합 충돌 예상: `TerrainComponent.*` · `TerrainColliderComponent.*`(`getBodyHandle` 을 `getBodyCount` 로 바꿈) · `FrameRenderer.cpp` · `TextureCache.*` · `docs/06_Backlog.md` · 이 문서(그 브랜치도 NEXT.md 를 고쳤으니 이 문서 쪽을 취하고 지형 항목만 지웁니다).
  남은 것(백로그 D2-5 (7)): Release 측정, 에디터 편집 모드에서 틱이 남은 청크를 처리하는지, 디바이스를 다시 만들면 칠한 스플랫이 사라지는 문제, 같은 텍스처를 쓰는 다른 지형도 함께 칠해지는 문제, 식생을 바뀐 셀만 다시 뿌리기, 존재하지 않는 `Environment/README.md`.
- `terrain2` 는 미커밋 변경이 없는지 확인하고, 통합 브랜치에 모아 전체 빌드로 확인한 뒤 병합합니다. `modgpu`(메시 모디파이어 컴퓨트 미리보기)는 병합했습니다.
- `D:\w\visual`: 병합은 끝났습니다. 에디터 기능 캡처(`captures/`)와 미추적 시나리오(`Resource/engine/automation/vis/`)가 남아 있으니 필요한 것만 챙기고 `py -3 -m Scripts worktree-remove visual --force` 로 지웁니다.
- `D:\w\perfbase` · `perfbase2` 는 성능 기준선이라 남깁니다.

## 2. 새 세션의 첫 일: 검증

오늘 병합마다 Debug 전체 빌드는 경고 0 · 오류 0 으로 확인했지만, 병합된 전체의 테스트는 한 번도 돌리지 않았습니다. 순서는 다음과 같습니다.

1. `ctest --preset Ninja-Debug-nogpu` 와 `-lint`.
2. Ninja-Shipping `all AllTests` 경고 0, Shipping `ctest -L nogpu`. Shipping 은 키트가 STATIC 이라 내보내기 매크로가 빈다는 것을 코드로만 확인했습니다.
3. `ctest -L hostgpu` 직렬. Release 빌드와 네 백엔드 스크린샷 sha 대조(기준 `D:\w\perfbase`).
4. 알려진 실패: `editor.uipreview` 가 속성 편집 단계(프레임 375)에서 DX12 · Vulkan · GL 모두 결정적으로 집니다. 포커스와 무관합니다. 의심 커밋은 `f46dc5fde` · `62bd5b3f7` · `999eef7d1` 이고 이분 탐색은 하지 않았습니다. `AppTest` 의 에디터 케이스가 이것 때문에 빨갛습니다. 백로그 A-1.
5. Linux 전용 파일은 Windows 에서 컴파일되지 않아 기계적 치환만 하고 짓지 못했습니다(`PosixCrashHandler.cpp` · `EpollStreamTransport.cpp` · `FileUtil.cpp` 의 `st_mtim` 분기 · `HardwareProbe.cpp`). 사용자가 요청할 때만 WSL 로 확인합니다.

## 3. 계획한 큰 일 (순서대로)

1. **정리 3 종과 폴더 쪼개기.** 조건 세 부분 이상 기존 위반 1,773 건(754 파일)은 영역별로 한 작업자가 맡습니다. 영역은 `Source` 하위, `Tools/ReflectionParser`, `Test`, `Tools/OnlineLoadBot` 입니다(사용자가 정한 범주). 이어서 C-28(`engine::getXxx` 약 826 줄) 과 폴더 쪼개기(`Editor/Common/Commands` 61 · `Engine/Object/GameObject` 45 · `Engine/Renderer/Frame` 43)를 합니다. 셋은 파일이 크게 겹치므로 한 번에 하나씩 합니다.
2. **구조 리팩토링과 데이터 주도 적용.** 조사 결과는 아래 4 절입니다. 사용자 결정이 필요한 것은 3 가지입니다.
3. **서브틱 활용 조사.** `PostUpdate` 로 옮긴 컴포넌트 41 개를 전수 분류합니다(5 절).
4. **그 밖.** 지형 후속(식생 · 내비 재계산, 콜라이더 부분 갱신 측정), 다중 월드 잔여(프리팹 창, 창별 선택과 전역 선택 연결, 스카이), 캐릭터 편집 (a) 미리보기 씬 이관과 (c) 슬라이더, 서버 디버그 잔여(클라이언트 해시, 어긋난 오브젝트 짚기), `nogpu` 샤드 결정, `TaskNode` · PCH · 에디터 기동 시간 측정, 열린 결함 A-2(TDR 뒤 행).

## 4. 구조 리팩토링과 데이터 주도 조사 결과

범주는 `Source`, `Tools/ReflectionParser`, `Test`, `Tools/OnlineLoadBot` 입니다. 숫자는 정규식 집계이고 판단이 들어간 곳은 추정입니다.

- 규모: 타입 7,727 개(`struct` 4,962 · `class` 2,004 · `enum` 760), 헤더 1,801 개. 한 곳에서만 쓰이는 최상위 타입이 2,443 개(참조 1 곳 996 개)입니다. 그중 제품 코드 참조 0 인 공개 타입이 654 개입니다.
- 가장 큰 문제 넷: 서비스 접근 창구가 여섯 갈래(`engine::getX` 693 · `getSubsystem<>` 432 · `getSceneService<>` 125 · `game::getService<>` 124 · `editor::getService<>` 110 · `getInstance()` 65), 온라인 키트 골격 복제(서비스 11 개에 타입 약 473 개), 얇은 위임 클래스(`*Catalog` 53 개, 호출처 0~2 곳인 `Util` 65 개), 이름 불일치(add / register / append, update / tick / step / advance, `xCount` 2,659 대 `countX` 163, `Settings` 중 31 개는 `Config`, 27 개는 `Def`).
- 큰 단위: 1,000 줄 넘는 파일 64 개, 메서드 50 개 넘는 클래스 38 개(`FrameRenderer` 129 · `InputMap` 126 · `AbilitySystemComponent` 109 · `UISystem` 100 · `InputManager` 97 · `GameObjectManager` 66).
- 데이터 주도 후보 순위 상위: 게임 · 키트 튜닝 상수를 `PROPERTY` 나 팩 설정으로, 작물 · 유닛 색을 `Def` 속성으로, `LoadBot` 동작 표 통합, 이벤트 필드 목록을 필드 표의 Emit 열에서 만들기, 컴포넌트 아이콘을 `REFLECT(Meta=Icon)` 으로, 확장자 → 종류 정본 하나로, `UIStyleField` 서술자 표, usage define 을 `ENUM` 이름에서 만들기.
- 순서가 필요한 사슬: 확장자 정본 → 계약 테스트를 DataKind 표로. `SaveGame` · `Def` 리플렉션 경로 → 테스트 하네스.
- **사용자 결정이 필요한 것.** ① 결정 기록 476 행(enum 이름 표 45 곳 유지)을 바꿀지, ② 테스트만 쓰는 공개 타입 654 개를 지울지(기존 결정 "사용처 0 공개 API 유지"와 충돌), ③ 서비스 접근 창구를 어떤 모양으로 통합할지(제안: 언리얼 `GetSubsystem<T>()` 관례).
- 영향 순위 20 개의 전체 표는 이 세션의 조사 보고에만 있었습니다. 새로 조사하려면 `scout` 에게 같은 항목을 시킵니다.

## 5. 서브틱 활용 조사

서브틱(`registerSubTick`)은 `GameDirectorComponent.cpp` 한 곳에서만 쓰이고, `TickPhase` 의 `Early` · `Late` · `Finalize` 는 정의 밖 사용이 0 입니다. 반면 `setTickGroup` 은 48 개 파일, `PostUpdate` 는 41 개 파일입니다.
주석을 읽은 14 개는 "디렉터(`PrePhysics`)가 이 프레임의 ○○ 를 끝낸 뒤에 읽는다" 였고(HarvestValley · NileCity · StarSkirmish 의 컴포넌트, 카메라 계열), 선행 조건(`getTickHandle()` 로 다른 오브젝트 컴포넌트에도 건다)이 이 의도를 정확히 표현합니다.

- 할 일: `PostUpdate` 41 개를 (렌더 직전이 필요한 것 / 특정 컴포넌트 뒤에 읽는 것 / 이유를 모르는 것) 로 분류합니다. 후보 몇 개를 실제로 바꿔 프레임 시간과 동작을 비교합니다. 이득이 없으면 서브틱 · `TickPhase` 를 삭제 후보로 구조 리팩토링 지도에 올립니다.
- 선행 조건이 있는 서브틱은 별도 스테이지로 가므로 지금 방식보다 느릴 수도 있습니다. 측정하지 않았습니다.

## 6. 확정된 방침

- 상용 엔진과 비교해 고릅니다. 근거는 결정 기록에 "~로 알고 있다" 로 적어 기억 기반임을 밝힙니다.
- 구조는 규모가 커도 장기 이득이면 과감히 바꿉니다. 래퍼 · 별칭 · 옛 이름 호환은 두지 않고 데이터도 다시 씁니다.
- ponytail 과 graphify 를 항상 켭니다(Headroom 은 삭제). 구조 개편을 시킬 때는 요청에 그 뜻을 분명히 적습니다. ponytail 의 "기존 구조 유지" 와 부딪히기 때문입니다.
- graphify 그래프는 작업 묶음을 띄우기 직전에 갱신합니다(`graphify update Source`, 약 4분, 증분 아님). 산출물을 저장소 루트 `graphify-out/` 으로 옮깁니다.
- 에이전트는 push 하지 않습니다. 에이전트를 띄울 때 종류와 모델을 답변에 적습니다.
- 검증 단계: Debug `all AllTests` 경고 0, `ctest -L "lint|nogpu"`, Shipping `all AllTests` 경고 0, Shipping `nogpu`, 해당하면 앱 실행과 모듈 리로드.
- 동시 빌드는 최대 둘입니다. 워크트리는 `py -3 -m Scripts worktree-make <이름> --base main` 으로 만들고, 병합이 맞는지 확인한 뒤 `py -3 -m Scripts worktree-remove <이름>` 으로 지웁니다.

## 7. 도구와 환경

- 병렬 작업자 규칙은 [WorkerRules.md](WorkerRules.md) 에 있습니다. 병합할 때 문서 충돌은 양쪽을 남기는 방식으로 풀었고, 생성 문서(`docs/Config/*`)는 `GenerateConfigReference.py` 로 다시 만듭니다.
- 가드 훅 `.claude/hooks/GuardCommands.py` 가 `git commit --no-verify` 와 링크가 든 폴더의 재귀 삭제를 막습니다. 새 에이전트부터 확실히 적용됩니다.
- `.mcp.json` 이 graphify MCP 서버를 띄웁니다(`graphifyy[mcp]` 필요, `graphify-out/` 이 있는 main 체크아웃에서). 승인 대기 상태일 수 있습니다. context7 은 사용자 전역 설정에 등록했고 Node 는 설치했습니다(새 터미널에서 PATH 에 잡힙니다).
- graphify 와 ponytail 스킬은 `.claude/skills/` 에 있고 추적하지 않습니다. 새 워크트리에는 복사해야 합니다.
- 이 PC 의 사용자 Scripts 폴더(`C:\Users\ssw\AppData\Roaming\Python\Python314\Scripts`)는 PATH 에 없습니다. `graphify` 를 부르려면 더합니다.
- WSL VM(Ubuntu-26.04)이 `wsl --shutdown` 후에도 1 초 안에 다시 뜹니다. VS Code 의 WSL 연결이나 다른 도구가 깨우는 것으로 보이며, 원인을 특정하지 못했습니다.

## 8. 알려진 함정

- **빌드 로그를 bash `grep` 으로 세지 않습니다.** PowerShell `Start-Process` 의 `*>` 리다이렉션은 로그를 UTF-16 으로 써서 `grep` 이 항상 0 을 돌려줍니다. 이 세션에서 그 방식으로 "경고 0" 이라 잘못 보고한 일이 있습니다. 로그를 `Out-File -Encoding utf8` 로 쓰고 `Select-String | Measure-Object` 로 셉니다.
- **`worktree-remove` 는 브랜치도 지웁니다.** 병합이 실패했는데 먼저 지우면 브랜치를 잃습니다(커밋 객체는 남으니 `git branch <이름> <해시>` 로 복구). 병합 뒤 `git merge-base --is-ancestor` 로 확인하고 지웁니다.
- **워크트리 지우기**: 링크(정션)를 링크로서만 먼저 끊습니다. 변수가 든 `rmdir` 는 안전장치가 막으니 `worktree-remove` 를 씁니다.
- **앱 창을 전경으로 올려 캡처하는 스크립트를 쓰지 않습니다.** `SetForegroundWindow` 와 `CopyFromScreen` 방식은 사용자 화면에 창이 튀어나오고 작업표시줄 미리보기가 섞입니다. 메모장을 띄워 포커스를 흉내 내는 일도 같습니다. 캡처는 `-gv_screenshot` 을 쓰되, 이것은 게임 뷰만 저장하고 에디터 UI 는 담지 못합니다. 에디터 UI 를 전경 없이 캡처하는 방법(예: 창 그래픽 캡처)이 필요합니다.
- Edit 도구와 `sed -i` 가 CRLF 파일을 LF 로 바꾼 적이 있습니다. 수정 뒤 `git diff --stat` 로 파일 전체가 바뀌지 않았는지 봅니다.
- 정렬 할당 객체를 기반 포인터로 지우면 배포본 힙이 깨집니다(Debug 는 조용). 엔진 정리를 정적 소멸에 맡기지 않습니다. 모듈 안 정적 객체의 소멸자에서 스레드를 join 하지 않습니다.
- 무효 bindless 인덱스(`0xFFFFFFFF`)는 DX12 페이지 폴트, Vulkan DEVICE_LOST 입니다. 셰이더를 바꾼 뒤에는 `App.exe --cook-shaders` 로 다시 굽습니다.
- `castTo<Component>( Component*, 타입 )` 은 항상 성공합니다. 종류 거르기는 `getTypeInfo()->isDerivedFrom( 타입 )` 입니다.
- 커밋 훅은 우회하지 않습니다. 스크립트로 파일을 고칠 때는 heredoc 대신 Write 도구로 scratchpad 파일을 만듭니다.
- 비결정적 실패는 한 번의 재현으로 확정하지 않고 3~5 회 다시 돌려 재현율을 적습니다.

## 9. 새 세션 시작 절차

1. 이 문서, [작업 방식](../11_Workflow.md), [작업자 규칙](WorkerRules.md), [백로그](../06_Backlog.md) 순서로 읽습니다. C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md).
2. `git worktree list` 로 남은 워크트리를 확인하고 1 절의 `terrain2` · `modgpu` 상태를 봅니다.
3. 2 절의 검증을 먼저 합니다.
4. 3 절의 큰 일을 사용자와 순서를 확인한 뒤 시작합니다. 시작 전에 graphify 그래프를 갱신합니다.
