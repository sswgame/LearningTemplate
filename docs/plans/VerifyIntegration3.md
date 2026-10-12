# 통합 3 검증 체크리스트

이 PC 에서는 빌드까지만 했다(테스트 · 실행 파일 실행 · 서버 기동 없음). 이 문서는 **검증을 마치면 통째로 지운다.** 지우기 전에 남길 교훈은 영역 README 의 "함정 · 계약" 절이나 [결정 기록](../09_Decisions.md)으로 옮긴다.
범위는 `8fce26aaf..6f3373831`(main, 81 커밋 · 316 파일)이다. 전체 이야기는 [인계 문서](NEXT.md)의 "통합 3" 절에 있다.

## 0. 시작 전

- 옛 빌드 폴더가 있으면 **`cmake --preset` 을 먼저 다시 돌린다.** 파일을 옮겼고(Console · Spatial · ProfilerTimeline → Core, Dismemberment → GameFramework 등) 코드젠이 낡은 헤더 목록을 들고 있으면 `missing and no known rule` 로 멈춘다.
- 쿠킹 셰이더는 일부러 바꾸지 않았다(`SWCOOK 3` 그대로). 쿠킹 도구 지문 커밋은 6 절에서 따로 처리한다.

## 1. 자동 확인

| 순서 | 명령 | 기대 |
|---|---|---|
| 1 | Debug `ctest -L lint`(프리셋 `Ninja-Debug-lint`) | 전부 통과. 새 게이트 `CheckPostTickThisBinding` 과 그 셀프 테스트 포함 |
| 2 | Debug `-L nogpu`(프리셋 `Ninja-Debug-nogpu`) | 전부 통과 |
| 3 | Debug `-L hostgpu` (`ctest --test-dir build/Ninja-Debug -L hostgpu -j 1`, 약 16 분) | 전부 통과 |
| 4 | Shipping `-L nogpu` · `-L hostgpu` | 전부 통과 |
| 5 | 게임 프리셋 넷(`Ninja-Debug-Shooter3D` · `-StarSkirmish` · `-HarvestValley` · `-VoxelCraft`) 빌드 | 2 차 병합 뒤 다시 짓지 않았다. 경고 0 · 링크 0 |

## 2. 새 테스트와 옮긴 테스트 — 먼저 볼 것

**처음 돌리는 테스트**(한 번도 실행되지 않았다. 기대값이 틀렸을 가능성부터 의심한다):

- `ReflectionEnumInfoTest` — `CameraRole` 의 옛 이름 `Editor` 가 `Viewport` 로 바뀌었다.
- `GameObjectManagerTest.PostTickOnComponentSkipsPendingDestroy` — 삭제 대기 컴포넌트는 틱 뒤 실행이 부르지 않는다.
- `GameObjectManagerPoolTest.ModuleUnloadDrainsDeferredPostTickWork` — 모듈을 내리기 전에 틱 뒤 대기열이 빈다.
- `ServiceClientCallTableTest.ForeignResponseDuringSendDoesNotTakeTheSendingDelegate` — 보내는 중에 온 다른 id 의 응답이 현재 델리게이트를 소비하지 않는다.
- 온라인 클라이언트 취소 테스트 둘(취소 뒤 응답이 와도 델리게이트가 불리지 않음 · 파괴 순서 양쪽) — 같은 테스트 파일에 있다.
- `TestGameService` 의 회수 테스트, `ModuleHostTest.SharedModuleCannotBeRegisteredForReload`.
- `PrefabTest.CookedPrefabMatchesItsSource` — 소스로 읽은 프리팹과 쿠킹본으로 읽은 프리팹을 JSON 으로 다시 써서 글 전체를 견준다. 틀리면 쿠킹본이 소스와 어긋난 것인지 테스트 쓰기가 불안정한 것인지부터 가른다.
- `TestRHISupport` 의 두 스레드 `tickCompleted` 테스트.
- `TestRenderPassGPU` 의 동시 `acquire` 테스트(DX12 · DX11 만. GL 은 다른 스레드에서 못 만든다). **작업 스레드 안의 `SW_TEST_DEFENSIVE_SCOPE` 가 스레드마다 따로 동작하는지** 먼저 본다.

**옮긴 테스트**(경로가 바뀌었다): `TestDismemberment`(GameFramework Combat), `TestWitcherRPG` · `TestTacticsSRPG`(`SkillTree` · `RunMap` 케이스), `TestProgression`, `TestSpatial`(CoreTest), `ProfilerTimeline` 테스트(CoreTest).
테스트 목록에서 사라지거나 두 번 잡히지 않았는지 `ctest -N` 개수를 병합 전과 견준다.

## 3. 동작이 바뀌는 곳 — 실기동으로 볼 것

| 변경 | 보는 것 | 어디가 깨지면 |
|---|---|---|
| `CameraRole::Editor` → `Viewport` | 에디터 씬 뷰 카메라가 게임 뷰 수집에서 빠지고 BugIt 캡처가 정상 | 에디터가 두 번 그려지거나 뷰가 비는 것 |
| 씬 무효화 델리게이트(구독 핸들) | 에디터에서 씬을 바꾼 뒤 되돌리기 기록이 비는지, 종료가 깨끗한지 | 종료 때 크래시, 씬 전환 뒤에도 되돌리기가 남는 것 |
| 틱 뒤 실행 도우미 12 곳 + `this` 6 곳 교체 | 탈것 · 말 · 카메라 감독 · 물 · 지형 · 식생 · 플레이어 청크가 평소처럼 동작 | 틱 뒤에 컴포넌트가 사라지는 장면(탑승 중 내림 · 오브젝트 파괴)에서의 크래시 |
| `GameService` 소유자 태그 회수 | 키트 하나만 모듈 리로드했을 때 서비스가 새 인스턴스를 가리키는지 | 리로드 뒤 옛 포인터 · bind 거절 로그 |
| `ObjectCopyBuffer` | 에디터에서 복사 → 에디터 모듈 리로드 → 붙여넣기가 동작 | 붙여넣을 것이 없다 |
| 환경설정 저장 | 환경설정을 고친 직후 리로드해도 값이 남는다 | 확장 섹션 값이 기본값으로 돌아감 |
| 공용 모듈 이름 거절 | `LiveReloadManager::registerModule` 이 공용으로 올린 이름을 거절한다 | GameFramework 를 리로드하려는 시도가 조용히 통과 |
| Shooter 적 체력 → `Vitality` | 체력이 0 아래로 안 내려가고, 최대 체력이 1 보다 작으면 1 이 된다. **데이터에서 체력 0 을 주면 예전에는 즉사였고 지금은 1 로 산다** | 적이 안 죽거나 HP 바가 이상함. 쓰러짐 · 절단 확인 |
| `TextureCache` 락 밖 로드 | 텍스처 로드 중 다른 키 조회가 막히지 않고, 같은 키 동시 요청이 한 번만 만든다 | 로드 중 크래시 · 리로드 교착(네 백엔드) |
| `RHIReleaseQueue` 락 | DX12 에서 두 스레드가 `tickCompleted` 를 불러도 안전 | 해제 목록 중복 · 누락 |
| 온라인 호출 표 | 응답이 보내는 중에 다른 id 로 와도 현재 호출이 안 사라진다. `clear()` 는 없어졌다 | 응답을 못 받는 호출 |
| 로그 범주 이름 | `AbilitySystemComponent*` · `ScheduleSystem*` 로그 태그가 파일 이름으로 바뀌었다. 로그를 걸러 보는 도구가 있으면 맞춘다 | 로그 필터가 안 맞음 |
| 개발 명령 · HUD 레지스트리 | 에디터 HUD 섹션 · 개발 콘솔 명령이 평소처럼 나온다(락을 추가함) | 모듈 로드 때 교착 |
| `isServiceBound<T>()` | 게임 인스턴스 시작 · `DontDestroyOnLoad` 가 평소처럼 동작 | 서비스가 묶였는데 안 묶였다고 나옴 |

실기동 순서는 [인계 문서](NEXT.md)의 "검증할 것"(에디터 `-EnableEditor` · 게임 프리셋 · 네 백엔드 · 모듈 리로드)을 따른다. **`-gv_profileFrames=60` 을 붙여 스스로 끝나게 한다.**

## 4. 정적으로 확인할 것 (실행 없이)

- `dumpbin /exports` 로 `GameFramework.dll` 이 `SW_GF_API` 심볼과 `bindGameService` · `unbindGameService`(C 링크) 를 내보내는지, `SWGame.dll` 이 그것을 가져오는지. 이 PC 에서는 `readobj` 로 한 번 봤다.
- **알려진 문제**: 키트 DLL 이 Base 클래스의 암시적 멤버를 내보낸다(백로그 1-6). 이번 병합 전부터 있었다.
- 리플렉션 생성 검사: (a) 같은 이름의 반사 헤더를 둘 만들면 구성이 `FATAL` 로 멈추는지 (b) 구성 뒤 기존 헤더에 반사 매크로를 넣으면 그 헤더가 바뀔 때 빌드가 서는지(`cmake/Engine/CheckReflectionCandidates.cmake`).

## 5. CI

main 으로 push 한 뒤의 첫 전체 CI 다. 그전에는 Lint 단계에서 막혀 있어 Windows · Linux 빌드와 테스트가 한 번도 돌지 않았다. 확인할 곳:

- Linux: `__builtin_return_address` 분기(`GameService` 소유자 태그), GLX 컨텍스트, 다중 상속 경고, `SceneNavigation` 의 `NavDebugView` 값 멤버(내보내지 않은 타입), `OpenGLRHICommandList.cpp`.
- 실패하면 [검증과 측정](../08_Verification.md) 4절의 `ci-jobs` 로 실패 주석을 읽는다.

## 6. 쿠킹 가능한 환경에서만 — 셰이더 쿠킹 스탬프

- 커밋 `8021b9337`(쿠킹 스탬프에 컴파일러 도구 지문, `SWCOOK 3` → `4`)은 통합에서 **일부러 뺐다**(`54b50db93` 이 되돌림). Shipping 쿠킹 단계가 `--verify-shaders`(`cmake/Engine/AssetAndToolTargets.cmake` 71 줄)로 쿠킹본과 스탬프를 대조하고, 체크인된 쿠킹 셰이더 약 3424 개와 `cook.stamp` 가 `SWCOOK 3` 이라 다시 쿠킹 없이는 빌드가 선다.
- 적용 순서: `54b50db93` 을 되돌린다 → `App.exe --cook-shaders` 로 다시 쿠킹한다 → 쿠킹 셰이더와 `cook.stamp` 를 같은 커밋에 넣는다 → Shipping 빌드가 `--verify-shaders` 를 통과하는지 본다.
- 문자열 · 포함 확인: `StringBuilder::append( string )` 과 `VulkanRHIRequiredVersion` include 가 빌드되는지.

## 7. 측정(Release)

성능 주장은 Release 측정으로만 한다([검증과 측정](../08_Verification.md) 2절). 이번 변경에서 볼 곳:

- 텍스처 로드 중 다른 스레드의 `find` · `acquire` 대기 시간(전 · 후).
- `EngineLoop.tick` 의 틱 뒤 실행 경로, `AudioEngine` 게시, `GT.GPUScene.build`.
- 컴파일 시간: `GameObject.h` 가 `TagSystem.h` 를 끌어오지 않는다. `-ftime-trace` 전후([BuildSpeed](BuildSpeed.md) 5 · 6 단계와 같이).

## 8. 끝낸 뒤 지울 것

- 이 문서와 [인계 문서](NEXT.md)의 "통합 3" 절 · "알아 둘 함정" 가운데 끝난 항목.
- 워크트리와 브랜치: `py -3 -m Scripts worktree-remove <이름>` 을 `tcore` `tinv` `tcombat` `tgfs` `tgfd` `tposttick` `utex` `upipe` `uxml` `ugfa` `ugfb` `uaud` `uonl` `umod` `uinc` `integ3` 에 한다. 지운 뒤 `git branch -d wt/<이름>`.
- 남은 일은 백로그로 옮긴 채 둔다(키트 DLL 암시적 멤버 내보내기 · `GameDataXML` 호출부 66 곳 · DX11/GL bindless 즉시 재사용 · 키트 루트 소스 10 개 이상 폴더 규칙).
