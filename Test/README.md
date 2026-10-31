# Test — 단위 테스트와 시나리오

> 질문: 테스트와 시나리오를 어디에 쓰고 어떻게 돌리며, 옛 틀의 무엇을 고쳤는가?
> 독자: 기능을 고치거나 더하는 사람과 에이전트.
> 결론: GPU 없이 되는 확인은 기능 폴더 옆 `Tests/` 의 `SW_TEST_CASE`(`App --test` 한 번이 모두 돈다), 그림 · 입력 · 씬이 돌아야 하는 확인은 `Content/**/automation/*.scenario.xml` 시나리오(적은 순서대로 도는 단계 · 백엔드별 골든 이미지)다. 이 폴더(`Test/`)는 CTest 등록만 한다.

## 무엇을 어디에

| 확인할 것 | 쓰는 곳 | 도는 방법 |
|---|---|---|
| 순수 식 · 형식 · 파일 형식 · 등록표 · 엔진 서비스(창 · GPU 없음) | `Source/<…>/<기능>/Tests/Test<이름>.cpp` | `App --test`(`ctest -L nogpu`) |
| 화면 · 입력 · 씬 · 백엔드 | `Content/<도메인>/automation/<이름>.scenario.xml` | `App -scenario=<리소스 경로> -unattended`(`ctest -L hostgpu`) |

## 단위 테스트

```cpp
#include "pch.h"

#include "Core/String/Base64Util.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

SW_TEST_CASE( Base64UtilTest, URLAlphabetHasNoPaddingAndRoundTrips )
{
    const uint8 arrByte[] = { 0xFB, 0xFF, 0xBF, 0x00, 0x10 };
    SW_EXPECT_EQUAL( string( "-_-_ABA" ), Base64Util::encodeURL( arrByte, sizeof( arrByte ) ) );
    vector<uint8> listDecoded;
    SW_REQUIRE_TRUE( Base64Util::decodeURL( "-_-_ABA", listDecoded ) ); // 실패하면 이 케이스를 여기서 끝낸다
    SW_EXPECT_EQUAL( sizeof( arrByte ), listDecoded.size() );
}
```

- 파일은 기능 폴더 옆 `Tests/` 에 두고 그 모듈과 함께 짓는다(Core · Engine · GameFramework · 키트 · 게임 모두). 목록이 없다 — 파일을 더하면 잡힌다. 배포 구성은 `Tests/` 와 `Core/Testing` 을 폴더째 짓지 않는다.
- 단언: `SW_EXPECT_TRUE` · `SW_EXPECT_FALSE` · `SW_EXPECT_EQUAL` · `SW_EXPECT_NEAR` · `SW_EXPECT_SNAPSHOT( 이름, 글 )`(케이스 옆 `Snapshots/<케이스>.<이름>.txt` 와 비교), `SW_REQUIRE_TRUE` · `SW_REQUIRE_EQUAL`(실패하면 케이스 끝).
- 케이스가 일부러 내는 Warning · Error 는 그 로그 id 로 `SW_EXPECT_LOG( "영역.사건" )` 에 적는다. 쓰기는 `test::makeScratchPath( "이름" )` 의 폴더에만 한다.
- 엔진 서비스(`engine::getSubsystem<T>()`)를 그대로 쓴다 — 헤드리스로 선 엔진 위에서 돈다.
- 선언 셋, 모두 끝에 옵션을 붙일 수 있다:

```cpp
SW_TEST_CASE( PoolTest, Grows, ._tags = "slow", ._timeoutSeconds = 60 ) { … }       // 함수 하나
SW_TEST_CASE( NetTest, Reconnects, ._disabledReason = "서버 픽스처를 고칠 때까지" ) { … } // 지우지 않고 끈다(보고에 까닭이 남는다)

struct TempSceneFixture { TempSceneFixture() { /* 준비 */ } ~TempSceneFixture() { /* 정리 */ } Scene _scene; };
SW_TEST_CASE_F( TempSceneFixture, SpawnsOne ) { SW_EXPECT_EQUAL( 0u, _scene.count() ); }  // 케이스마다 새 픽스처

static constexpr uint32 kArrSize[] = { 1, 2, 64 };
SW_TEST_CASE_EACH( PoolTest, KeepsCapacity, kArrSize ) { SW_EXPECT_TRUE( Pool( param ).capacity() >= param ); } // PoolTest.KeepsCapacity/0 · /1 · /2
```

- 케이스 시한은 기본 30 초다(`._timeoutSeconds`). 넘기면 `[ TIMEOUT ]` 에 이름을 남기고 실행을 끝낸다(종료 코드 12) — 멈춘 케이스는 되살릴 수 없다. 디버거가 붙어 있으면 시한을 보지 않는다. 크래시 보고의 문맥에는 지금 케이스(`TestCase`)가 남는다.

```
App --test                                  # 모두(순서는 섞이고 씨앗이 찍힌다)
App --test --test-filter=LODViewTest.*      # 와일드카드, 쉼표로 여럿, 앞에 - 면 뺀다
App --test --test-seed=2741926653           # 찍힌 순서를 다시
App --test --test-repeat=20                 # 간헐 실패 찾기
App --test --update-snapshots               # 스냅숏을 지금 값으로(바뀐 파일을 리뷰한다)
App --test --test-filter=[slow]             # 태그로(빼기는 -[slow])
App --test --test-shard=1/4                 # 이름 해시로 나눈 4 몫 가운데 1 번(CTest SW_UNIT_TEST_SHARDS)
App --test --test-run-disabled              # 꺼 둔 케이스도(고친 뒤 확인)
```

케이스 하나는 이럴 때 실패다: 단언 실패 · 단언이 하나도 없음 · 적지 않은 Warning · Error 로그 · 적었는데 나오지 않은 로그 · 다른 스냅숏. 거르개에 맞는 케이스가 없으면 실행이 실패다(샤드가 빈 것은 실패가 아니다). 보고 JSON 은 `Saved/Test/<프로세스>/report.json`(찍힌다).

## 시나리오

```xml
<Scenario name="engine.debughud" fixedDelta="0.0166667" timeoutFrames="300" startAfter="ScenePlaying" startTimeoutFrames="900" input="exclusive">
	<Launch variable="gv_firstScene" value="game/empty/maps/fogshowcase.scene.xml"/>  <!-- 기동 때만 정해지는 값(ctest 가 명령줄로 옮긴다) -->
	<AllowLog id="Audio.DeviceLost" reason="CI 러너에 오디오 장치가 없다"/>           <!-- 이 시나리오에서 나와도 되는 로그 id -->
	<DevCommand line="hud off"/>
	<WaitUntil watch="DebugHUD.Shown" equals="0" timeoutFrames="60"/>                  <!-- 조건이 설 때까지 -->
	<Press slot="Key.LeftControl"/>
	<Wait frames="1"/>                                                                   <!-- 시간은 Wait 만 흘린다 -->
	<Tap slot="Key.F3"/>
	<Screenshot file="hud.ppm"/>
	<ExpectImage file="hud.ppm" golden="hud" region="0,0,0.3,0.45"/>                    <!-- 백엔드별 골든과 비교 -->
	<Pass/>
</Scenario>
```

- 단계는 **적은 순서대로** 돈다. 시간은 `<Wait frames="N"/>`(또는 `seconds`)가 흘리고, `<WaitUntil>` 은 조건이 설 때까지 기다린다(제 프레임을 혼자 갖도록 읽기가 앞뒤를 한 프레임씩 띄운다). `<At frame>` 은 없다.
- `timeoutFrames` 는 시작한 뒤 지난 **모든** 프레임(조건 대기 포함)을 센다. `<Pass/>` 가 뒤의 일(아래 끝 차단)을 기다리는 프레임만 빼고 센다 — 그 대기는 `kMaxFinishWaitFrames` 가 따로 묶는다.
- 이미지는 골든 비교다: 영역을 가로 160 화소 이하로 줄여 `golden/<시나리오>.<이름>.<백엔드>.ppm` 과 견주고, 색 거리 12 이상인 화소가 `tolerance`(기본 1 %)를 넘으면 실패다. 골든은 `-scenario-update-golden` 으로 뜨거나 갱신하고(보고의 `updatedGoldens`), 그림을 보고 커밋한다.
- 로그 게이트: 시나리오 구간의 Warning · Error 는 `<ExpectLog>`(기대) 나 `<AllowLog>`(허용, 사유 필수)가 없으면 실패다. id 없는 줄은 허용할 수 없다 — 소스에 id 를 붙인다.
- 산출물(사진 · 보고)은 `Saved/Automation/<시나리오>/<프로세스>/` 에 쓴다. 무인 실행은 사용자 설정 파일을 읽지도 쓰지도 않고 사양 자동 프리셋을 쓰지 않는다.
- 씬 · 연출 단계: `<LoadScene path="…" timeoutFrames="600"/>`(씬을 열고 설 때까지 붙잡는다), `<PlaySequence object="이름"/>` · `<WaitSequence object="이름"/>`(그 오브젝트의 시퀀서 재생 · 끝 기다림).
- 벤치마크: `<BeginFrameStats/>` … `<EndFrameStats file="bench.json"/>` 사이의 벽시계 프레임 시간을 요약해 산출물 폴더에 쓴다(평균 · p95 · p99 · fps). 예: `game.empty.benchmark`.
- **재생**(테스트가 아니라 게임 · 콘솔에서): `ScenarioPlayback::play( 경로, 선택 )` 또는 콘솔 `scenario.play <경로> [loop] [demo] [fixed]` · `scenario.stop`. 판정이 끝나도 프로세스를 끝내지 않고, 반복(어트랙트) · 사람이 손대면 멈춤(데모) · 고정 프레임 시간(벤치마크)을 고른다. 끝은 델리게이트로 알린다.
- 하위 폴더(예: `rhiswap/` — 네 백엔드가 모두 있어야 한다)는 조건이 붙은 시나리오라 ctest 가 줍지 않는다.
- 키트 · 에디터 확장에 기대는 시나리오는 루트에 `requiresModule="GF_EditorPlugin_Admin"`(쉼표로 여럿)을 적는다 — 그 모듈이 꺼진 빌드(그 키트를 쓰지 않는 활성 게임)에서는 건너뜀이고, 정적 검사도 이름을 보지 않는다. 언리얼에서 끈 플러그인의 테스트가 없는 것과 같다.
- 편집기 시나리오 실행(`-EnableEditor -scenario=…`)은 사용자의 `Saved/Editor` 를 읽지도 쓰지도 않는다 — 프로세스마다 `Saved/Automation/EditorState/editor_p<pid>` 를 비우고 쓴다(`-gv_editorStateDir` 로 바꾼다).

종료 코드: 0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀 · 15 처리하지 않은 로그.

## 시나리오 실행 — 체인 · 결정성 · 시간

- **백엔드당 프로세스 하나**: CTest 항목 `Scenario.<게임>` 하나가 시나리오 모두를 체인(`-scenario=a,b,c -scenario-keep-going`)으로 돌리고, `-scenario-backends=all` 이 백엔드마다 자식 App 하나를 띄운다(창은 2×2 바둑판 `-WinX/-WinY`). 단계마다 세션이 빈 씬으로 앞 씬을 비우고 → 전역 변수를 세션 시작 값으로 되돌리고 → `<Launch>` 값을 건 뒤 → 게임을 다시 세운다(`AutomationSession::startStage`). 렌더 파이프라인 변수는 실행 중에 다시 고른다(`FrameRendererSubsystem`). 둘째 단계부터는 게임 인스턴스를 상태 복원 없이 다시 세운다(`IGameRestarter` — App 이 `GameModuleManager` 를 호스트 범위에 건다. 게임을 내리고 → 활성 씬을 닫고 → 월드를 플레이 전으로 두고 → 새로 만든다, 언리얼 PIE 의 새 GameInstance). 그래서 단계마다 게임의 기동 흐름(첫 씬 · 벤치)이 첫 단계와 같은 길로 돌고, 시나리오 모두가 체인 하나다(벤치 · kaykit 포함 22 단계). 전체 ctest 85 초(처음 272 초), 동시 App 최대 5(부모 + 넷). 단계별 결과는 `Testing/Logs/Scenario.<게임>.<백엔드>.log` 의 `chain stage i/n`.
- **에셋 도착**: 시나리오 세션은 `gv_finishAssetWorkEachFrame=1` 도 건다 — 프레임 경계에서 렌더 스레드가 받은 패킷을 다 끝낸 뒤 에셋 로드 단계 · 스트리밍 큐 태스크 · 비동기 IO 가 함께 빌 때까지 기다리고(`AssetManager::finishPendingAssetWork`, 언리얼 FlushAsyncLoading), 렌더 스레드는 텍스처 업로드를 상한 없이 내고 끝남까지 기다리며(`TextureCache::finishPendingLoads`) 머티리얼 초기화 · 밉 스트리밍 상한을 풀고, 머티리얼 PSO 변형을 빌더 스레드 대신 그 프레임에 만든다. 그래서 요청한 에셋 · PSO 는 늘 같은 프레임에 보인다 — 예전에는 셰이더 캐시가 빈 첫 실행에서 디더 · runner 가 달랐다. 확인은 `Bin/Saved/ShaderCompileCache` · `ShaderCache` 를 지운 ctest.
- **결정적 실행**: 시나리오 세션은 `gv_animationWaitForClips=1` 을 건다 — 애니메이터가 시작 때 비동기로 읽는 클립을 `onBeginPlay` 에서 기다린다(언리얼 자동화의 FlushAsyncLoading). 끄면 첫 포즈가 나오는 프레임이 디스크 지연에 달려 래그돌 · 물리 이력이 갈렸다. 물리는 구성마다 같은 비트다: Jolt 는 `CROSS_PLATFORM_DETERMINISTIC=ON`(`ThirdParty/joltphysics/vcpkg-port`), `Engine/Physics` 의 TU 는 FMA 축약을 끈다(`sw_markDeterministicSources` — 파괴와 같은 규칙). 렌더는 대상이 아니다(디버그 셰이더 코드젠 · 렌더 수학의 FMA 로 전체 해상도 바이트는 다르고, 골든 비교 크기에서는 같다). 남은 문턱 안 차이(vfxgpulights 0.14 % · kaykitrig vk 0.28 %)는 그 렌더 차이다.
- **셰이더**: 소스 정책(개발 실행)은 쿠킹본을 읽지 않는다 — 로컬 캐시(`Bin/Saved/ShaderCache`) → 실시간 컴파일, 컴파일 결과 캐시는 `Bin/Saved/ShaderCompileCache`(언리얼 DDC). 리플렉션도 그 바이트코드에서 실행 중에 만든다. 실시간 컴파일의 기본은 쿠킹과 같은 최적화 코드젠이고 디버그 코드젠은 `-gv_shaderDebugCodegen=1`(RenderDoc, 언리얼 `r.Shaders.Optimize=0`). 쿠킹본 정책(배포 · `-cooked-content`)은 팩의 쿠킹본만 읽는다. 굽기는 `<쿠킹 폴더>/<도메인>/shaders/bin/<백엔드>` 에 쓰고(씬 · 프리팹과 같은 스테이징), 팩 굽기(`--cook-all` · `--cook-packs`)가 늘 바뀐 것만 다시 굽고 판정한 뒤 담는다. 소스 트리의 `shaders/bin` 은 낡은 잔재다(쿠킹본 접미사가 `Cooked` 역할이라 팩이 경고하고 뺀다).
- **시작 시점의 결정성**: 로딩 화면은 씬 로드 받아들임 알림(`SceneManager::onSceneLoadRequested`, 요청마다 `SceneLoadKind` 필수 — 편집기 · 진단은 `Tool`)에 게임이 붙어 띄우고(언리얼 PreLoadMap), 떠 있는 동안 게임 시간을 멈춘다(`LoadingScreen` 형식). 그래서 시나리오의 첫 프레임은 늘 월드의 첫 틱이다 — 예전에는 로드 시간 · 최소 표시 시간만큼 월드가 로딩 화면 뒤에서 돌았다(cloth 골든을 다시 떴다).
- **종료 코드**: 표 하나 `Config/Engine/ProcessExitCodes.json` → C++ `ProcessExitCode` · CMake `SW_EXIT_CODE_<이름>`.
- **남은 일**: 창 바둑판 간격은 창 틀 두께를 Win32 대략 값으로 둔다(`AutomationBackendMatrix` 의 shortcut) — 모니터가 2.6K×1.5K 보다 작으면 겹친다. 플랫폼 작업 영역 · 창 틀 크기 API 가 생기면 그 값으로.
- **겹치는 쓰기를 막은 곳**: 로그 파일은 싱크가 안다(`ILogSink::getLogFilePath` — BugReport · 콘솔 패널이 폴더의 "가장 새 파일" 을 고르지 않는다), 같은 초의 폴더 · 사진 이름은 `FileUtil::reserveUniquePath`(만들기가 곧 자리 잡기), 개발 TLS 인증서는 한 PEM(`devserver.pem`).
- **시간 기록**: 실행이 끝날 때마다 `[Scenario] <이름> timeline:` 한 줄이 남는다 — 프레임 종류(시작 · 붙잡기 · 단계 · 고정 대기)별 ms 와 무거운 단계 종류, 가장 긴 대기. 단계 사이 초기화는 `stage reset took`. 무엇이 느린지는 새로 재지 말고 이 줄을 모은다. `<WaitUntil>` 이 기다리지 않고 지나가면 `was already true` 가 남는다 — 고정 대기를 조건 대기로 바꿀 때 부정형 단언(일어나지 않음을 보는 것)을 가려낸다.
- **끝에서만 기다리는 일**: 판정이 다음 단계 진행에 쓰이지 않는 일은 프레임에서 뺀다. 패킷 스크린샷 · 편집기 UI 캡처의 파일 쓰기는 워커가 하고, `<ExpectImage>` 는 미뤄 두었다가 `<Pass/>` 가 남은 쓰기 · 끝 차단 조건(`SW_AUTOMATION_FINISH_BLOCKER`)을 기다린 뒤 한 번에 판정한다. 그래서 한 시나리오 안에서 같은 파일을 두 번 찍을 수 없다(읽을 때 거절).
- **편집기 자체 테스트**: `SW_EDITOR_SELF_TEST` 케이스는 `<EditorSelfTest pattern="*"/>` 단계(`engine/automation/editor/selftests.scenario.xml`)로 `Scenario.editor` 체인에서 돈다 — 백엔드 넷과 로그 게이트를 함께 받는다(Vulkan 검증 레이어의 오류도 실패다).

## 옛 틀에서 고친 것

| 옛 문제 | 지금 |
|---|---|
| 소스를 거울로 따르는 `Test/` 트리 · 링크마다 실행 파일 · TestPolicy.json(라벨 · 샤드 · 비용표) | 기능 옆 `Tests/`, `App --test` 하나, 정책 파일 없음 |
| 배포에서만 지는 테스트 · 구성마다 다른 케이스 수 | 배포는 테스트를 짓지 않음, 보고에 돈 케이스 수 |
| 앞 케이스에 기대는 순서 의존 | 매번 섞는 순서 · 씨앗 · `--test-repeat` |
| 표식 없는 경고 · 부분 문자열로 가리기 | 케이스 · 시나리오 단위 로그 게이트, id 로만 선언 |
| 0 건 통과 | 단언 없는 케이스 · 빈 실행은 실패 |
| 멈춘 케이스는 CTest 시한까지 이름 없이 매달림 · 크래시는 어느 케이스인지 모름 | 케이스 시한 감시(`[ TIMEOUT ]` + 이름) · 크래시 문맥 `TestCase` |
| 픽스처 · 매개변수 · 태그 · 끄기가 없어 복사 · 주석 처리로 대신 | `SW_TEST_CASE_F` · `SW_TEST_CASE_EACH` · `._tags` · `._disabledReason` |
| 케이스가 늘면 한 프로세스가 다 돎 | `--test-shard=i/n` + CTest `SW_UNIT_TEST_SHARDS` |
| 고정 이름 쓰기 위치(병렬 · 반복 회차 충돌) | 프로세스 · 케이스별 폴더 |
| 손으로 옮겨 적는 기대값 · `measured` · `provisional` · 미확정(14) | 스냅숏 · 골든(자동 기록) |
| `<At frame=N>` 절대 프레임 · 고정 대기 뒤 확인 | 순서대로 도는 단계 · `WaitUntil` |
| 조건 대기 프레임을 세지 않는 시한 | 모든 프레임을 세는 시한 |
| 전역 허용 목록(범위 · 계획 칸 수작업) | 시나리오 안 `<AllowLog id reason>` |
| 기계의 사용자 설정 · 사양 프리셋이 그림을 바꿈 | 무인 실행은 실행별 설정 파일 · 프리셋 고정 |
| 시나리오 속성은 앱을 띄워야 검사 | `ScenarioFilesTest` 가 엔진 안에서 모든 시나리오의 이름 · 속성 검사 |

## 왜 이 모양인가

- 언리얼은 단위 테스트(Automation Spec — 엔진 안에서 서비스를 쓰며 돈다)와 기능 테스트(Functional Test · Gauntlet)를 나눈다. 여기도 같다.
- 테스트 틀을 직접 둔 까닭: 엔진의 문자열 · 서식 · 로그 · 등록표(모듈 언로드 때 빠진다)를 그대로 쓰고 외부 의존을 늘리지 않는다.
