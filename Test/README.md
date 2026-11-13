# Test — 단위 테스트와 시나리오

> 질문: 테스트와 시나리오를 어디에 쓰고 어떻게 돌리는가?
> 독자: 기능을 고치거나 더하는 사람과 에이전트.
> 결론: GPU 없이 되는 확인은 기능 폴더 옆 `Tests/` 의 `SW_TEST_CASE`(`App --test` 한 번이 모두 돈다), 그림 · 입력 · 씬이 돌아야 하는 확인은 `Content/**/automation/*.scenario.xml` 시나리오(적은 순서대로 도는 단계 · 백엔드별 골든 이미지)다. 이 폴더(`Test/`)는 CTest 등록만 한다.

## 무엇을 어디에

| 확인할 것 | 쓰는 곳 | 도는 방법 |
|---|---|---|
| 순수 식 · 형식 · 파일 형식 · 등록표 · 엔진 서비스(창 · GPU 없음) | `Source/<…>/<기능>/Tests/Test<이름>.cpp` | `App --test`(`ctest -L nogpu`) |
| 화면 · 입력 · 씬 · 백엔드 | `Content/<도메인>/automation/<이름>.scenario.xml` | `App -scenario=<리소스 경로> -unattended`(`ctest -L hostgpu`) |

모든 ctest 항목은 먼저 `Import.heightfields` · `Import.textures` · `Import.models`(고정 단계 `ImportedAssets`)를 거친다 — 스탬프가 낡은 원본만 다시 임포트하고 맞으면 곧바로 끝난다. 형식 · 임포터 버전을 올린 묶음도 손으로 임포트하지 않는다.

## 단위 테스트

```cpp
#include "pch.h"

#include "Core/Encoding/Base64Util.h"
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
- 키트 · 게임 케이스는 그 키트를 올린 프로세스에서만 등록된다. 함께 짓는 게임마다(`SW_GAMES`, Release) `UnitTests.<게임>` 이 그 게임으로 떠서 싣는 키트 · 게임 폴더의 케이스만 돌고(`--test-source=폴더,폴더`), 어느 게임도 쓰지 않는 키트(`SW_BUILD_UNUSED_KITS`, Release)는 App 이 모두 올리므로 ctest `UnitTests` 에 든다 — 그래서 게임이 아직 없는 키트에도 호스트를 따로 두지 않는다. Debug 는 `SW_GAMES` 가 비어 활성 게임이 싣지 않는 키트를 짓지도 않는다 — 키트를 고친 묶음은 Release 가 확인한다.
- 에디터 케이스(경로에 `/Editor/` 가 든 파일 — `Source/Editor` · 플러그인 · 키트의 에디터 확장)는 `App --test` 에 없다(그 단계에 에디터 모듈이 아직 없다). 숫자를 볼 때 `Scenario.editor` 의 `editor.unitTests` 를 따로 본다.
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

- 테스트 `.cpp`(`/Tests/`)는 유니티 묶음에 들지 않는다 — 파일마다 익명 이름공간의 짧은 이름(`kNowMs` · `…TestUtil`)이 묶음 경계가 바뀔 때마다 다른 쌍으로 겹쳤다(X11 소스를 빼는 규칙과 같은 자리).
- 원소 형식에 `const utf8* _pTestName` 칸을 두면 매개변수 케이스 이름이 번호가 아니라 그 글이다(`PoolTest.KeepsCapacity/small`) — 배열 순서를 바꿔도 같은 입력을 가리킨다.
- `._bIsolated = true` 인 케이스는 실행기가 이 실행 파일을 자식으로 다시 띄워(`--test-isolated-case=<이름>`) 그 하나만 돌린다. 크래시 · 교착 · 강제 종료가 그 케이스의 실패(자식 출력 꼬리와 함께)로 남고
  나머지 케이스 · 보고는 이어진다. 자식 기동 비용이 붙으니 프로세스를 죽일 수 있는 케이스만 쓴다. App 에 등록된 케이스만 된다(에디터 케이스는 격리하지 못한다).
- CTest 항목은 JSON 보고를 `<빌드>/Testing/Reports/<항목>.json` 에 남긴다(판 번호 `schemaVersion` · 케이스마다 이름 · 파일 · 줄 · 회차 · 격리 · 결과 · 시간 · 실패 글).
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

프로세스를 죽이거나(크래시 보고) 멈출 수 있는 일(교착) · 표준 출력 · 표준 에러 · 프로세스 전역 상태(전역 싱크 떼기)는 자식 역할로 본다. 자식 역할은 평소 실행 · 목록 · 케이스 수에서 빠지고, 부모 케이스가 이 실행 파일을 자식으로 다시 띄워 그 하나만 돌린다(같은 판정 규칙, 통과하면 종료 코드 0):

```cpp
SW_TEST_CHILD_ROLE( LogTest, ConsoleChild ) { const string mode = test::getChildRoleArgument(); … }  // 인자는 환경 변수 대신 명령줄로
SW_TEST_CASE( LogTest, ConsoleReachesStdout, ._timeoutSeconds = 90 )                                  // 자식 App 기동만 Debug 에서 몇 초 — 넉넉히
{
    const test::ChildRunResult child = test::runChildRole( "LogTest.ConsoleChild", "probe", 45.0f ); // 시한을 넘기면 죽이고 _bTimedOut
    SW_EXPECT_EQUAL( 0, child._exitCode );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( child._output, "probe" ), child.getOutputTail() );    // 표준 출력 · 에러를 합친 글
}
```

손으로 자식 하나를 다시 돌리려면 `App --test --test-child-role=LogTest.ConsoleChild --test-child-arg=probe -unattended`. 자식의 로그 파일은 부모 케이스의 쓰기 폴더에 남는다.

시간이 판정에 들어가는 케이스는 잠들지 않는다. 시각을 읽는 객체는 시계(`IClock`)를 받고(`GameTimer` · `FixedTimestep` · `NetRequestClient` · `ProfilerPerformanceModel` …) 테스트는 `ManualClock` 을 밀어 정확한 값을 단언한다. 다른 스레드 · OS 가 상태를 바꾸기를 기다릴 때는 사건(문 · future · 완료 핸들)을 기다리고, 사건이 없는 상태(카운터 · 연결 수)는 `test::waitUntil`(`Core/Testing/TestWait.h` — 통과는 조건, 시한은 멈춘 실행 그물)로 묻는다. "20 ms 자면 됐겠지" 는 느린 기계에서 옳은 구현을 지게 한다. 남겨도 되는 잠은 실제 시계 자체를 시험하거나 실시간 멈춤이 입력일 때, 그리고 경합 창을 넓혀 결함을 드러낼 기회만 주는 잠(옳은 구현은 잠 길이와 무관하게 통과)뿐이다.

케이스 하나는 이럴 때 실패다: 단언 실패 · 단언이 하나도 없음 · 적지 않은 Warning · Error 로그 · 적었는데 나오지 않은 로그 · 다른 스냅숏. 거르개에 맞는 케이스가 없으면 실행이 실패다(샤드가 빈 것은 실패가 아니다). 보고 JSON 은 `Saved/Test/<프로세스>/report.json`(찍힌다).

### 하네스

- 네트워크: Core 엔드포인트는 `LoopbackCluster`, 온라인 키트 끝단은 `OnlineHostTestUtil.h`(`OnlineTestFixture` — 호스트 + 클라이언트 묶음, 키트 DLL 이 include 하고 키트 서버 쪽은 같은 프로토콜 항목의 가짜로 올린다).
- 그 밖: `AnimationTestUtil` · `RHIFakeDevice` · `FakeFontRasterizer`, 자식 프로세스는 `Core/Process` · 크래시 보고.
- 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release — 봇이 온라인 키트를 모두 싣는다)가 돌린다.
- 화면이 있어야 하는 것은 시나리오가 맡는다: `UIPreviewModel`(`editor/uipreview` · `uipreviewdesigner`), 섀도 복사본의 성공 길(핫 리로드 시나리오), 화면이 뜨는 게임 흐름(`loadingscreen`), `Editor/Common` · `Editor/Panels` 의 그리기 코드.
- 폴더 집계 `py -3 Scripts/dev/TestCoverage.py` 는 테스트가 다른 폴더에 있어도 "없음" 으로 센다 — 실제로 비었는지는 클래스 이름을 테스트에서 찾아 본다.

## 리플렉션 파서

`ReflectionParser` 는 Engine 을 링크하지 못해 위 틀에 들어가지 못한다. `Test/ReflectionParser/Fixtures/<이름>.h` 하나가 `ReflectionParser.<이름>` 하나이고(`ctest -L nogpu`), 실제 실행 파일을 빌드와 같은 인자로 돌려 헤더 맨 앞의 기대 줄(`// EXPECT-ERROR:` · `EXPECT-GEN:` · `EXPECT-NOT-GEN:` · `EXPECT-TYPE:`)과 대조한다 — 형식은 `RunParserFixture.cmake`. 잘못된 입력을 일부러 담으므로 린트 범위(`Source/`) 밖에 둔다.

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
	<ExpectImagesDiffer first="off.ppm" second="on.ppm" region="0,0,1,1" minDiffering="0.02"/> <!-- 같은 실행의 두 장이 정말 다른지(음성 대조, 골든 없음) -->
	<ExpectImagesDiffer first="full.ppm" second="lod.ppm" minDiffering="0" maxDiffering="0.01"/> <!-- 비용을 줄이는 값이 그림을 깨지 않는지(상한, 골든 없음) -->
	<Pass/>
</Scenario>
```

- 장치 능력이 없어 그 그림을 그리지 않는다고 선언된 백엔드는 `<SkipUnless watch="RHI.MaterialTessellation" reason="…"/>` 로 건너뛴다(감시 값 0 이면 Skipped, 못 읽으면 실패). 능력이 없는 백엔드에서 조용히 통과하거나 오류 로그로 지지 않게 하는 자리다 — 예: `automation/displacement`(OpenGL · WebGPU 는 머티리얼 테셀레이션이 없다).
- 단계는 **적은 순서대로** 돈다. 시간은 `<Wait frames="N"/>`(또는 `seconds`)가 흘리고, `<WaitUntil>` 은 조건이 설 때까지 기다린다(제 프레임을 혼자 갖도록 읽기가 앞뒤를 한 프레임씩 띄운다). `<At frame>` 은 없다.
- `<WaitUntil>` 의 시한은 셋 중 하나다: `timeoutFrames`(시나리오 프레임), `timeoutSeconds`(게임 초 — 고정 델타로 프레임이 된다), `timeoutWallSeconds`(벽시계 초 — 게임 시간과 무관한 바깥 일: 자식 프로세스 종료 · 파일 · GPU). 무인 실행은 프레임이 실시간보다 빨리 돌아 게임 초로 바깥 일을 기다리면 금방 끝난다(넷플레이 종료 대기 20 초가 0.8 초였다).
  바깥 일(자식 프로세스 · 컴파일러 · 파일 감시 · 워커 비동기 로드 · GPU 회수 — 예: `Scene.Transitioning` · `Assets.Pending`)을 따르는 감시 값은 `SW_AUTOMATION_WALL_WATCH` 로 등록하고, 그 값을 기다리는 `<WaitUntil>` 이 `timeoutWallSeconds` 가 아니면 로드 오류다 — 프레임은 실시간보다 빨리 돌아 프레임 시한이 몇 초인지 정하지 못한다(패키징 1400 프레임이 1 초였다).
  벽시계 시한은 멈춘 실행을 잡는 안전 그물이다 — 통과는 조건이 서는 것이고 시한이 지나면 실패(`WaitUntil gave up`)다. 시한이 통과를 정하는 단계(정해진 시간 동안 "일어나지 않음" 을 보는 대기 따위)는 만들지 않는다 — 그런 판정은 프레임 · 사건으로 센다. 단계의 벽시계 시한(`timeoutWallSeconds` · `ExpectExitWithin` · `CloseWindow withinSeconds`)도 전체 그물처럼 `gv_scenarioWallScale` 로 늘어난다(`AutomationRunner::scaleWallSeconds`) — 느린 기계에서 옳게 늦은 실행이 단계 시한에서 지지 않게.
- 에디터 시나리오는 고정 대기 대신 `<EditorWaitSettled/>` 로 **에디터가 멈출 때까지(정지 상태)** 기다린다 — 화면(창 크기 요청 반영 · 도크 배치 적용 끝 · 밀린 흉내 입력 없음 · 앞 단계 뒤에 그린 화면이 두 번 연속 같은 배치), 씬(전환 없음), 에셋(씬이 기다리는 로드 · 업로드 · PSO 없음), 뒤에 끝날 일(끝 차단 `SW_AUTOMATION_FINISH_BLOCKER` — 스크린샷 · 로컬 저장소 쓰기 · 에디터 UI 캡처 쓰기 · 썸네일 캡처, 비동기 일을 새로 만들면 그 모듈이 등록한다). "앞 단계 뒤" 는 프레임 수가 아니라 에디터가 다음에 여는 프레임에 찍는 기다림 번호로 가린다. 일어나지 않음을 보는 단언(`sameAs` · 실패 수 0)도 이 뒤에 둔다 — "N 프레임 동안 안 바뀌었다" 는 증명이 아니고, 정지 상태에서 본 값이 그 답이다. 다른 프로세스의 답(넷플레이)은 받은 수 감시 값을 `Remember` 하고 바뀔 때까지 벽시계로 기다린다. 고정 대기는 시간 자체가 시험 내용일 때만 쓴다(누른 채인 구간 · "다음 프레임에 깨어났다" 같은 프레임 단언). 붙잡는 단계는 무엇을 기다리는지 한 줄을 반드시 넘기고(`holdUntilReady` 의 필수 인자) 벽시계 상한에 걸리면 그 줄이 실패 까닭에 붙는다. 진행 중인 일에는 단계 시한이 없다 — 그 일의 시간은 Debug · 부하 · 크기에 달려 어떤 숫자도 임의다(끝나지 않으면 시나리오 벽시계 상한 · 멈춤 감시가 잡는다). 할 일이 다 끝났는데 화면이 120 프레임 넘게 계속 움직이면 진동하는 UI 결함으로 진다.
- `timeoutFrames` 는 시나리오에 적은 프레임(고정 대기 · 단계)만 센다. 시계를 붙잡는 대기(`<WaitUntil>` · `<LoadScene>` · 입력 대상 자리 잡기 · 스크린샷의 에셋 대기 …)는 저마다 제 시한이 있어 늦게 선 조건은 그 단계 이름으로 진다(언리얼 latent 명령 · 유니티 `WaitUntil` 과 같고, 전체는 벽시계 `wallTimeoutSeconds` 가 묶는다). 마지막 단계가 시한 밖이면 읽기 오류다. `<Pass/>` 가 뒤의 일(아래 끝 차단)을 기다리는 프레임은 `kMaxFinishWaitFrames` 가 따로 묶는다.
- 시나리오 이름 · 속성은 앱을 띄우지 않고 단위 테스트 `ScenarioFilesTest` 가 엔진 안에서 모두 검사한다.
- 이미지는 골든 비교다: 영역을 가로 160 화소 이하로 줄여 `golden/<시나리오>.<이름>.<백엔드>.ppm`(백엔드는 쿠킹 표의 명령줄 이름 `dx12` · `vk` · `wgpu` …, Linux 는 `<백엔드>.linux.ppm` — 편집기 글꼴과 렌더러가 달라 따로 둔다)과 견준다(`AutomationGoldenUtil`).
  - **판정은 두 축 · 세 상한이다.** 넓고 얕은 차이: 색 거리(R · G · B 차의 합) 12 이상인 화소의 비율 ≤ `tolerance`, 평균 거리 ≤ `maxMeanDistance`. 좁고 깊은 차이: 거리 96 이상인 화소가 4 이웃으로 이어진 가장 큰 덩어리 ≤ `maxRegionPixels`(축소 그림의 화소 수) — 비율만 보면 작은 마젠타 덩어리(전체의 0.2 %)가 1 % 아래로 지나갔다. 기본 문턱은 데이터 `Config/Dev/GoldenImage.json`(칸이 없으면 이미지 단언이 그 이유로 진다)이고 `<ExpectImage>` 속성이 덮는다. 기본값의 근거: 비율 1 % 는 옛 판정 그대로, 덩어리 16 은 백엔드 사이 골든 921 쌍(옛 판정 통과)의 최대 9 위, 평균 0.005 는 같은 쌍의 최대 0.0011 위(2026-11 측정).
  - 판정은 `Pass` · `PassNearLimit`(한 축이라도 상한의 `_nearLimitRatio`=절반을 넘음) · `Fail` 한 식이다(`AutomationGoldenUtil::judge`). 일반 실행의 근접은 통과이고 보고 `nearLimitGoldens[]` 와 경고 `Automation.GoldenNearLimit`(실행기 `[Scenario]` 줄이라 로그 게이트가 세지 않는다 — 골든의 여유 문제지 판정 대상의 결함이 아니다)로 알린다.
  - `-scenario-update-golden` 은 없거나 · 크기가 다르거나 · 근접이거나 · 진 골든만 다시 쓴다(통과는 두어 리뷰할 바이트를 늘리지 않는다 — 예전에는 진 골든만 써 0.99 % 로 남은 골든이 다음 실행 1.02 % 로 졌다). 쓸 때 옆에 메타 `*.golden.json`(커밋 `git describe --dirty` · 백엔드 · 까닭 · 쓰기 전 골든과의 측정값 · 그때의 상한)을 같이 쓴다. 보고의 `updatedGoldens` 를 보고 그림을 확인한 뒤 커밋한다.
  - 기록 모드가 아닌 실행은 보고 `goldenSamples[]` 에 골든마다 세 측정값과 상한을 남긴다. 실행 사이 흔들림(문턱이 빡빡한지)은 여러 보고를 모아 `py -3 Scripts/dev/ScenarioCulprit.py --golden-margins [보고 폴더…]` 로 본다(골든별 최대값 · 여유 = 상한 / 최대값).
  - 검증 체인 백엔드(쿠킹 표 `scenario_backends`)마다 골든이 있는지는 단위 테스트 `ScenarioGoldensTest` 가 본다 — 체인에 백엔드를 더하면(WebGPU) 골든을 뜰 때까지 진다. 백엔드를 고르는 시나리오(`<SkipUnless>` · `<SwapBackend>`)는 하나 이상이면 된다.
  - 편집기 시나리오는 그림을 찍기 전에 `<Variable name="gv_editorUiScale" value="1.5"/>` 로 UI 배율을 고정한다 — 배치가 모니터 DPI 를 따르면 다른 PC 에서 크기부터 달라진다.
- 사진에 드는 것: 개발 겹치기 화면(`UIScreen::isDevOverlay` — 디버그 HUD 등)은 게임 UI 와 따로 칠해 렌더러가 캡처를 백버퍼로 옮긴 **뒤에** 그린다(`FramePassExecutor::drawDevOverlay`, 에디터 게임 뷰는 `rendertarget/dev_overlay` 를 겹치고 UI 캡처는 그 텍스처를 뺀다) — 스크린샷 · 골든에 들지 않는다. 게임 UI(토스트 포함)는 든다. `<ExpectUI screens>` 는 게임 화면만 센다(`UISystem::getGameScreenCount`) — HUD 를 띄운 실행과 아닌 실행의 판정이 같게. 디버그 드로우 · 테스트 그림 겹치기(`CanvasOverlays`)는 아직 주 출력에 칠한다.
- 판정 색: 스테이지가 돌았는지 보는 클리어 · 표시 색은 마젠타를 쓰지 않는다 — 마젠타는 엔진 전체에서 오류 · 빠진 텍스처 표시다(지오메트리 · 테셀레이션 시나리오는 청록).
- 쓰는 곳 세기: 리플렉션 컴포넌트 · 감시 값 · 개발 명령은 콘텐츠 · 시나리오 · 코드에서 참조가 하나는 있어야 한다(`py -3 -m Scripts lint feature-refs`, 커밋 훅이 돈다). 시나리오로 못 잇는 것은 `Config/Rules/Lint/FeatureReferenceExceptions.json` 에 까닭과 함께 둔다 — 그 표가 곧 남은 배치 목록이고, 참조가 생기면 줄을 지우라고 린트가 알린다.
- 로그 게이트: 시나리오 구간의 Warning · Error 는 `<ExpectLog>`(기대) 나 `<AllowLog>`(허용, 사유 필수)가 없으면 실패다. id 없는 줄은 허용할 수 없다 — 소스에 id 를 붙인다.
- 사건 세기: `<ExpectLog id="영역.사건">` 만 쓴다 — 세려는 줄은 소스에서 `SW_LOG_INFO_ID` · `SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID` 로 쓴다. `contains` 는 그 id 줄의 값(경로 · 수)을 좁힐 때만 붙인다(문장으로 세면 문구를 고칠 때 단언이 낡는다). 개발 명령의 답은 `DevCommand.Reply` 줄이다.
- 에디터 콘솔은 지금 단계가 `<ExpectLog>` 로 선언한 줄을 단위 테스트의 `SW_EXPECT_LOG` 처럼 흐리게 보인다 — 세션(`AutomationSession`)이 든 로그 판정 `AutomationLogPolicy` 한 벌을 게이트 · 재생기 · 콘솔이 나눠 들고, 읽기 규칙(`<AllowLog>` 허용 · `<ExpectLog>` 선언 · `count="0"` 은 선언 아님)은 `AutomationLogPolicy::prepare` 한 곳이다. `<AllowLog>` 줄은 경고색 그대로다(언리얼 `AddExpectedError` 도 기대 오류만 따로 표시).
- 산출물(사진 · 보고)은 `Saved/Automation/<시나리오>/<프로세스>/` 에 쓴다. 무인 실행은 사용자 설정 파일을 읽지도 쓰지도 않고 사양 자동 프리셋을 쓰지 않는다.
- 씬 · 연출 단계: `<LoadScene path="…" timeoutFrames="600"/>`(씬을 열고 설 때까지 붙잡는다), `<ExpectActiveScene path="…"/>`(활성 씬이 그 리소스에서 왔는지 — 세이브 불러오기의 씬 옮김), `<PlaySequence object="이름"/>` · `<WaitSequence object="이름"/>`(그 오브젝트의 시퀀서 재생 · 끝 기다림).
- 벤치마크: `<BeginFrameStats/>` … `<EndFrameStats file="bench.json"/>` 사이의 벽시계 프레임 시간을 요약해 산출물 폴더에 쓴다(평균 · p95 · p99 · fps). 예: `game.empty.benchmark`.
- **재생**(테스트가 아니라 게임 · 콘솔에서): `ScenarioPlayback::play( 경로, 선택 )` 또는 콘솔 `scenario.play <경로> [loop] [demo] [fixed]` · `scenario.stop`. 판정이 끝나도 프로세스를 끝내지 않고, 반복(어트랙트) · 사람이 손대면 멈춤(데모) · 고정 프레임 시간(벤치마크)을 고른다. 끝은 델리게이트로 알린다. 재생은 테스트 세션(`AutomationSession` — 체인 · 판정 · 종료 코드)과 나뉜 객체 `ScenarioPlayer` 가 하고(언리얼 Level Sequence Player 와 Automation 세션의 나눔), 같은 실행기 · 단계를 쓴다.
  시퀀서 항목 종류 `Scenario`(`_scenario="….scenario.xml"`)는 시작 프레임을 지날 때 이 창구로 시나리오를 부른다 — 연출 안에 게임 행동 · 입력을 끼우는 자리(언리얼 시퀀서 이벤트 트랙).
  행동 층 리플레이(고스트 · 킬캠 · 버그 재현 — `.swintent`)는 같은 실행기에서 `<Intent>` · 개발 명령 `intent.play` 로 돈다(기록 조종자 `IntentTrackControllerComponent` 를 함께 쓴다). 카트 고스트(`KartGhost`)는 고정 걸음 차 입력을 양자화한 키트 형식이라 따로다.
- 단계 종류는 그 기능의 모듈이 제 .cpp 에서 `SW_AUTOMATION_STEP` 로 등록한다 — 엔진 · GameFramework · 에디터와 같이 **키트 · 게임**도(예: GF_OpenWorldWestern 의 `<FeedHorse horse food>`, `game.wildwest.mount`). 모듈을 내리면 빠지고 핫 리로드는 같은 이름을 갈아 끼운다. 정적 검사(`UnitTests.<게임>`)는 그 게임과 키트를 올린 채 이름을 본다.
- 하위 폴더(예: `rhiswap/` — 네 백엔드가 모두 있어야 한다)는 조건이 붙은 시나리오라 ctest 가 줍지 않는다. 게임의 하위 폴더 다섯만 게임마다 따로 항목이 된다(`<팩>` 은 팩 폴더 이름, 활성 게임이 아니면 `-game=<게임>`): `showcase/`(쇼케이스 씬을 찍는 시나리오 — 게임 체인과 같은 길의 체인 `Scenario.<팩>.showcase`, 골든은 `showcase/golden/`), `desktop/`(OS 전역 상태를 쓰거나 프로세스를 끝내는 시나리오 — 하나씩 혼자, `Scenario.<팩>.desktop.<이름>`), `sequence/<묶음>.1` · `.2`(같은 사용자 설정 파일로 두 번 기동, `Scenario.<팩>.sequence.<묶음>`), `editor/`(그 게임을 싣는 편집기 체인 `Scenario.<팩>.editor` — 엔진 편집기 시나리오는 `Scenario.editor`), `load/`(`<Load durationSeconds="N"/>` 측정 창과 단계가 둘 다 끝나면 끝나는 부하 시나리오 — 엔진 `engine/automation/load/` 도. 하나씩 혼자 기본 백엔드로, `Scenario.<팩>.load.<이름>`. 라벨이 `load` 하나라 `-L hostgpu` 에 들지 않고 `ctest -L load` 로 돈다).
- 키트 · 에디터 확장에 기대는 시나리오는 루트에 `requiresModule="GF_EditorPlugin_Admin"`(쉼표로 여럿)을 적는다 — 그 모듈이 꺼진 빌드(그 키트를 쓰지 않는 활성 게임)에서는 건너뜀이고, 정적 검사도 이름을 보지 않는다. 언리얼에서 끈 플러그인의 테스트가 없는 것과 같다.
- 편집기 시나리오 실행(`-EnableEditor -scenario=…`)은 사용자의 `Saved/Editor` 를 읽지도 쓰지도 않는다 — 프로세스마다 `Saved/Automation/EditorState/editor_p<pid>` 를 비우고 쓴다(`-gv_editorStateDir` 로 바꾼다).

종료 코드: 0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀 · 15 처리하지 않은 로그.

## 시나리오 실행 — 체인 · 결정성 · 시간

- **게임마다 체인 하나**: 구성이 짓는 게임마다(활성 게임 `SW_ACTIVE_GAME` + 함께 짓는 게임 `SW_GAMES` — Release 는 `ALL`) 그 게임의 `Content/game/<팩>/automation/*.scenario.xml` 이 체인 `Scenario.<팩>` 이 된다. 팩 이름은 게임 프리셋 `Config/Game/<게임>.json` 의 `_packRoot` 다. 활성 게임이 아닌 게임의 체인은 `-game=<게임>` 으로 `Bin/Games/<게임>` 을 싣는다. 엔진 시나리오(`engine/automation/`)는 활성 게임의 체인에 든다. 그래서 Debug(활성 게임 Empty 하나)는 `Scenario.empty` 만, Release 는 모든 게임의 체인을 돈다 — 활성 게임만 돌면 다른 게임이 뜨지 않는 결함이 ctest 에 보이지 않는다.
- **백엔드당 프로세스 하나**: CTest 항목 `Scenario.<팩>` 하나가 시나리오 모두를 체인(`-scenario=a,b,c -scenario-keep-going`)으로 돌리고, `-scenario-backends=all` 이 백엔드마다 자식 App 하나를 띄운다(창은 2×2 바둑판 `-WinTile/-WinTileColumns` — 칸 간격은 자식 창이 제 틀 두께로 정하고, 원점은 주 모니터 작업 영역이며 작은 모니터에서는 겹침을 고르게 나눠 모든 창이 작업 영역 안이다). 단계마다 세션이 빈 씬으로 앞 씬을 비우고 → 전역 변수를 세션 시작 값으로 되돌리고 → `<Launch>` 값을 건 뒤 → 게임을 다시 세운다(`AutomationSession::startStage`). 렌더 파이프라인 변수는 실행 중에 다시 고른다(`FrameRendererSubsystem`). 둘째 단계부터는 게임 인스턴스를 상태 복원 없이 다시 세운다(`IGameRestarter` — App 이 `GameModuleManager` 를 호스트 범위에 건다. 게임을 내리고 → 활성 씬을 닫고 → 월드를 플레이 전으로 두고 → 새로 만든다, 언리얼 PIE 의 새 GameInstance). 그래서 단계마다 게임의 기동 흐름(첫 씬 · 벤치)이 첫 단계와 같은 길로 돌고, 시나리오 모두가 체인 하나다. 단계별 결과는 `Testing/Logs/Scenario.<팩>.<백엔드>.log` 의 `chain stage i/n`.
- **에셋 도착**: 시나리오 세션은 결정적 실행 정책(`DeterministicRun` — `Engine/Foundation/DeterministicRun.h`)의 `_bFinishAssetJobsEachFrame` 도 켠다 — 프레임 경계에서 렌더 스레드가 받은 패킷을 다 끝낸 뒤 에셋 로드 단계 · 스트리밍 큐 태스크 · 비동기 IO 가 함께 빌 때까지 기다리고(`AssetManager::finishPendingAssetJobs`, 언리얼 FlushAsyncLoading), 렌더 스레드는 텍스처 업로드를 상한 없이 내고 끝남까지 기다리며(`TextureCache::finishPendingLoads`) 머티리얼 초기화 · 밉 스트리밍 상한을 풀고, 머티리얼 PSO 변형을 빌더 스레드 대신 그 프레임에 만든다. 그래서 요청한 에셋 · PSO 는 셰이더 캐시가 빈 첫 실행에서도 늘 같은 프레임에 보인다. 확인은 `Bin/Saved/ShaderCompileCache` · `ShaderCache` 를 지운 ctest.
- **결정적 실행**: 시나리오 세션은 같은 정책의 `_bWaitForAnimationClips` 를 켠다(배포본에는 채우는 쪽이 없어 늘 꺼짐. 환경 시간 멈춤 `_bFreezeEnvironmentTime` 은 시나리오가 `-gv_environmentAnimate=0` 으로 고른다) — 애니메이터가 시작 때 비동기로 읽는 클립을 `onBeginPlay` 에서 기다린다(언리얼 자동화의 FlushAsyncLoading). 끄면 첫 포즈가 나오는 프레임이 디스크 지연에 달려 래그돌 · 물리 이력이 갈린다. 물리는 구성마다 같은 비트다: Jolt 는 `CROSS_PLATFORM_DETERMINISTIC=ON`(`ThirdParty/joltphysics/vcpkg-port`), `Engine/Physics` 의 TU 는 FMA 축약을 끈다(`sw_markDeterministicSources` — 파괴와 같은 규칙). 렌더는 대상이 아니다(디버그 셰이더 코드젠 · 렌더 수학의 FMA 로 전체 해상도 바이트는 다르고, 골든 비교 크기에서는 같다). 골든 문턱 안의 백엔드 · 구성 차이는 그 렌더 차이다.
- **셰이더**: 소스 정책(개발 실행)은 쿠킹본을 읽지 않는다 — 로컬 캐시(`Bin/Saved/ShaderCache`) → 실시간 컴파일, 컴파일 결과 캐시는 `Bin/Saved/ShaderCompileCache`(언리얼 DDC). 리플렉션도 그 바이트코드에서 실행 중에 만든다. 실시간 컴파일의 기본은 쿠킹과 같은 최적화 코드젠이고 디버그 코드젠은 `-gv_shaderDebugCodegen=1`(RenderDoc, 언리얼 `r.Shaders.Optimize=0`). 쿠킹본 정책(배포 · `-cooked-content`)은 팩의 쿠킹본만 읽는다. 굽기는 `<쿠킹 폴더>/<도메인>/shaders/bin/<백엔드>` 에 쓰고(씬 · 프리팹과 같은 스테이징), 팩 굽기(`--cook-all` · `--cook-packs`)가 늘 바뀐 것만 다시 굽고 판정한 뒤 담는다. 소스 트리의 `shaders/bin` 은 낡은 잔재다(쿠킹본 접미사가 `Cooked` 역할이라 팩이 경고하고 뺀다).
- **시작 시점의 결정성**: 로딩 화면은 씬 로드 받아들임 알림(`SceneManager::onSceneLoadRequested`, 요청마다 `SceneLoadKind` 필수 — 편집기 · 진단은 `Tool`)에 게임이 붙어 띄우고(언리얼 PreLoadMap), 떠 있는 동안 게임 시간을 멈춘다(`LoadingScreen` 형식). 그래서 시나리오의 첫 프레임은 늘 월드의 첫 틱이다.
- **종료 코드**: 표 하나 `Config/Engine/ProcessExitCodes.json` → C++ `ProcessExitCode` · CMake `SW_EXIT_CODE_<이름>`.
- **겹치는 쓰기를 막은 곳**: 로그 파일은 싱크가 안다(`ILogSink::getLogFilePath` — BugReport · 콘솔 패널이 폴더의 "가장 새 파일" 을 고르지 않는다), 같은 초의 폴더 · 사진 이름은 `FileUtil::reserveUniquePath`(만들기가 곧 자리 잡기), 개발 TLS 인증서는 한 PEM(`devserver.pem`).
- **시간 기록**: 실행이 끝날 때마다 `[Scenario] <이름> timeline:` 한 줄이 남는다 — 프레임 종류(시작 · 붙잡기 · 단계 · 고정 대기)별 ms 와 무거운 단계 종류, 가장 긴 대기. 단계 사이 초기화는 `stage reset took`. 무엇이 느린지는 새로 재지 말고 이 줄을 모은다. `<WaitUntil>` 이 기다리지 않고 지나가면 `was already true` 가 남는다 — 고정 대기를 조건 대기로 바꿀 때 부정형 단언(일어나지 않음을 보는 것)을 가려낸다.
- **어디서 시간이 드는가**: 게임 시나리오의 고정 대기는 `fixedDelta` 아래 시뮬레이션 프레임 수라(카메라 · 애니메이션이 자리 잡는 시간 — 골든이 그 순간을 찍는다) 조건 대기로 바꾸지 않는다. 무거운 장면은 포커스 없는 백엔드 넷이 함께 돌면 프레임이 크게 느려져 함께 도는 이득이 없으므로 그 체인은 `-scenario-backend-slots=1`(`Test/Scenario/CMakeLists.txt` 의 `swSequentialScenarioPacks`)로 차례로 돈다. 가벼운 체인은 기동이 겹쳐 함께 도는 편이 빠르다. 백엔드 교체 시간은 `Backend swap with modules took` 줄(엔진 · 모듈 몫)과 `ImGuiEditor.ShutdownMs` · `EditorContext.ShutdownMs`(에디터 몫, 서브시스템별)가 나눈다.
- **끝에서만 기다리는 일**: 판정이 다음 단계 진행에 쓰이지 않는 일은 프레임에서 뺀다. 패킷 스크린샷 · 편집기 UI 캡처의 파일 쓰기(편집기 캡처는 RGBA 풀기 · 마젠타 비율까지)는 워커가 하고, `<ExpectImage>` · `<CaptureEditorUI maxMagentaFraction>` 은 미뤄 두었다가(`AutomationStepContext::deferExpectAtMost`) `<Pass/>` 가 남은 쓰기 · 끝 차단 조건(`SW_AUTOMATION_FINISH_BLOCKER`)을 기다린 뒤 한 번에 판정한다. 그래서 한 시나리오 안에서 같은 파일을 두 번 찍을 수 없다(읽을 때 거절).
- **백엔드 사이의 기록 대조**: `<Record watch="…"/>` 는 값을 보고의 `records[]` 에 남기고, 부모 App(`AutomationPerBackend`)가 시나리오마다 통과한 백엔드들의 기록을 순번대로 견준다 — 하나라도 다르면 실행이 실패(10)이고 로그에 `record mismatch - <시나리오>: record #N '<이름>': dx11=… vk=…` 가 남는다(부모 보고의 `recordMismatches`). 게임 상태는 CPU 에서만 정해지므로 같아야 한다(예: `game.nilecity.determinism`). `<Record>` 가 있는 시나리오는 `independent` 로 나눌 때(`-scenario-backend-split=independent`)도 모든 백엔드에서 돈다(Debug 체인의 `all` 은 모두 하나씩 — 대조는 Release 가 맡는다).
- **보이는 실행의 상태 표시**: 창이 보이는 실행(`--show` · `SW_SHOW_WINDOWS=1`, `-unattended` 없음)은 디버그 HUD 맨 위에 Scenario 섹션(`DebugHUDSectionFlag::kAutomationRun` — 시나리오 · 체인 n/N · 단계 · 상태 · 첫 실패 줄 · Warning/Error 수)을 그린다. 실행기는 HUD 를 모르고 HUD 가 `IAutomationRuntime::findRunStatus` 만 읽는다. 사용자 설정에 쓰지 않고(`gv_debugHUDScenario` 로 끈다), 감시 값 `DebugHUD.Shown` · `SectionCount` 는 사용자 HUD 만 센다 — 보이는 실행과 숨김 실행의 판정이 같다.
- **고정 대기 줄이기**: 게임 시간이 드는 구간(메워짐 · 젖음 · 노출 적응처럼 속도 × 게임 시간인 값)만 `<Variable name="gv_timeScale" value="4"/>` … `value="1"` 로 감싸 같은 게임 시간을 1/4 프레임에 돈다. 사진 · 그 앞 프레임은 배율 1 로 둔다. 시나리오 전체 배율은 기각했다(Empty 체인 배율 4 — 프레임 11% 감소, 94 단계 중 17 실패, 대부분 고정 프레임 대기). 시간을 멈춘 시나리오는 대기 길이가 그림을 바꾸지 않으니 가장 긴 갱신 주기의 배수까지 줄인다(LOD 셋 240 → 48). 오래 걸리는 이동은 프레임 수 대신 목표 값 조건 대기로(`viewportgrid` — 카메라 높이).
- **편집기 자체 테스트**: `SW_EDITOR_SELF_TEST` 케이스는 `<EditorSelfTest pattern="*"/>` 단계(`engine/automation/editor/selftests.scenario.xml`)로 `Scenario.editor` 체인에서 돈다 — 백엔드 넷과 로그 게이트를 함께 받는다(Vulkan 검증 레이어의 오류도 실패다). 케이스가 앞 일(패널 열기 · 도킹 · DPI · 레이아웃 적용)의 효과를 기다릴 때는 `EditorSelfTestStep::WaitStable` 를 돌려 `<EditorWaitSettled/>` 와 같은 정지 판정(`EditorStabilityWait`)을 받는다 — `Continue` 는 프레임 자체가 시험 내용일 때(프레임마다 한 걸음인 입력 · 매 프레임 크기 바꾸기 · "다음 프레임에 그려졌다")와 조건을 직접 볼 때만 쓴다.

## 왜 이 모양인가

- 언리얼은 단위 테스트(Automation Spec — 엔진 안에서 서비스를 쓰며 돈다)와 기능 테스트(Functional Test · Gauntlet)를 나눈다. 여기도 같다.
- 테스트 틀을 직접 둔 까닭: 엔진의 문자열 · 서식 · 로그 · 등록표(모듈 언로드 때 빠진다)를 그대로 쓰고 외부 의존을 늘리지 않는다.
