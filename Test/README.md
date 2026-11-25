# Test — 단위 테스트

GPU 없이 되는 확인은 기능 폴더 옆 `Tests/` 의 `SW_TEST_CASE` 로 쓰고 `App --test` 한 번이 모두 돈다. 그림 · 입력 · 씬이 돌아야 하는 확인은 시나리오다([Scenario/README](Scenario/README.md)). 이 폴더(`Test/`)는 CTest 등록만 한다. 지킬 규칙은 [Testing](../docs/agents/Testing.md).

| 확인할 것 | 쓰는 곳 | 도는 방법 |
|---|---|---|
| 순수 식 · 형식 · 파일 형식 · 등록표 · 엔진 서비스(창 · GPU 없음) | `Source/<…>/<기능>/Tests/Test<이름>.cpp` | `App --test`(`ctest -L nogpu`) |
| 화면 · 입력 · 씬 · 백엔드 | `Content/<도메인>/automation/<이름>.scenario.xml` | `App -scenario=<리소스 경로> -unattended`(`ctest -L hostgpu`) |

모든 ctest 항목은 먼저 `Import.assets`(텍스처 · 모델 · 높이장, 고정 단계 `ImportedAssets`)를 거친다. 스탬프가 낡은 원본만 다시 임포트하므로 형식 · 임포터 판을 올린 묶음도 손으로 임포트하지 않는다.

## 케이스 쓰기

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

- 파일은 기능 폴더 옆 `Tests/` 에 두고 그 모듈과 함께 짓는다(Core · Engine · GameFramework · 키트 · 게임). 등록 목록은 없다. 배포 구성은 `Tests/` 와 `Core/Testing` 을 폴더째 짓지 않는다.
- 테스트 `.cpp`(`/Tests/`)는 유니티 묶음에 들지 않는다(익명 이름공간의 짧은 이름이 묶음 경계에 따라 겹친다).
- 단언: `SW_EXPECT_TRUE` · `SW_EXPECT_FALSE` · `SW_EXPECT_EQUAL` · `SW_EXPECT_NEAR` · `SW_EXPECT_SNAPSHOT( 이름, 글 )`(케이스 옆 `Snapshots/<케이스>.<이름>.txt` 와 비교), `SW_REQUIRE_TRUE` · `SW_REQUIRE_EQUAL`(실패하면 케이스 끝).
- 케이스가 일부러 내는 Warning · Error 는 로그 id 로 `SW_EXPECT_LOG( "영역.사건" )` 에 적는다. 쓰기는 `test::makeScratchPath( "이름" )` 폴더에만 한다.
- 엔진 서비스(`engine::getSubsystem<T>()`)는 그대로 쓴다. 케이스는 헤드리스로 선 엔진 위에서 돈다.
- 시각을 읽는 객체는 시계(`IClock`)를 받고 테스트는 `ManualClock` 을 민다. 다른 스레드 · OS 를 기다릴 때는 사건(문 · future · 완료 핸들)을, 사건이 없는 상태(카운터 · 연결 수)는 `test::waitUntil`(`Core/Testing/TestWait.h`)을 쓴다. 잠을 남겨도 되는 곳은 실제 시계 자체를 시험하거나 실시간 멈춤이 입력일 때, 경합 창을 넓히기만 하는 잠뿐이다.

선언은 셋이고, 모두 끝에 옵션을 붙일 수 있다.

```cpp
SW_TEST_CASE( PoolTest, Grows, ._tags = "slow", ._timeoutSeconds = 60 ) { … }       // 함수 하나
SW_TEST_CASE( NetTest, Reconnects, ._disabledReason = "서버 픽스처를 고칠 때까지" ) { … } // 지우지 않고 끈다(보고에 까닭이 남는다)

struct TempSceneFixture { TempSceneFixture() { /* 준비 */ } ~TempSceneFixture() { /* 정리 */ } Scene _scene; };
SW_TEST_CASE_F( TempSceneFixture, SpawnsOne ) { SW_EXPECT_EQUAL( 0u, _scene.count() ); }  // 케이스마다 새 픽스처

static constexpr uint32 kArrSize[] = { 1, 2, 64 };
SW_TEST_CASE_EACH( PoolTest, KeepsCapacity, kArrSize ) { SW_EXPECT_TRUE( Pool( param ).capacity() >= param ); } // PoolTest.KeepsCapacity/0 · /1 · /2
```

- 원소 형식에 `const utf8* _pTestName` 칸을 두면 매개변수 케이스 이름이 번호 대신 그 글이 된다(`PoolTest.KeepsCapacity/small`).
- `._bIsolated = true` 인 케이스는 실행기가 이 실행 파일을 자식으로 다시 띄워(`--test-isolated-case=<이름>`) 그 하나만 돌린다. 크래시 · 교착이 그 케이스의 실패로 남고 나머지는 이어진다. 기동 비용이 붙으니 프로세스를 죽일 수 있는 케이스만 쓴다. App 에 등록된 케이스만 된다(에디터 케이스는 안 된다).
- 케이스 시한은 기본 30 초(`._timeoutSeconds`)다. 넘기면 `[ TIMEOUT ]` 에 이름을 남기고 실행을 끝낸다(종료 코드 12). 디버거가 붙어 있으면 시한을 보지 않는다.

프로세스를 죽이거나 멈출 수 있는 일 · 표준 출력 · 표준 에러 · 프로세스 전역 상태(전역 싱크 떼기)는 자식 역할로 쓴다. 자식 역할은 평소 실행 · 목록 · 케이스 수에서 빠지고, 부모 케이스가 자식으로 띄워 그 하나만 돌린다.

```cpp
SW_TEST_CHILD_ROLE( LogTest, ConsoleChild ) { const string mode = test::getChildRoleArgument(); … }  // 인자는 환경 변수 대신 명령줄로
SW_TEST_CASE( LogTest, ConsoleReachesStdout, ._timeoutSeconds = 90 )                                  // 자식 App 기동만 Debug 에서 몇 초
{
    const test::ChildRunResult child = test::runChildRole( "LogTest.ConsoleChild", "probe", 45.0f ); // 시한을 넘기면 죽이고 _bTimedOut
    SW_EXPECT_EQUAL( 0, child._exitCode );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( child._output, "probe" ), child.getOutputTail() );    // 표준 출력 · 에러를 합친 글
}
```

자식 하나를 손으로 돌리려면 `App --test --test-child-role=LogTest.ConsoleChild --test-child-arg=probe -unattended`. 자식의 로그 파일은 부모 케이스의 쓰기 폴더에 남는다.

## 판정 · 보고

케이스는 단언 실패 · 단언 없음 · 적지 않은 Warning · Error 로그 · 적었는데 나오지 않은 로그 · 다른 스냅숏일 때 진다. 거르개에 맞는 케이스가 없으면 실행이 진다(샤드가 빈 것은 아니다). 보고 JSON 은 `Saved/Test/<프로세스>/report.json`, CTest 항목은 `<빌드>/Testing/Reports/<항목>.json`(`schemaVersion`, 케이스마다 이름 · 파일 · 줄 · 회차 · 격리 · 결과 · 시간 · 실패 글)에 남는다.

## 실행

```
App --test                                  # 모두(순서는 섞이고 씨앗이 찍힌다)
App --test --test-filter=LODViewTest.*      # 와일드카드, 쉼표로 여럿, 앞에 - 면 뺀다
App --test --test-seed=2741926653           # 찍힌 순서를 다시
App --test --test-repeat=20                 # 간헐 실패 찾기
App --test --update-snapshots               # 스냅숏을 지금 값으로(바뀐 파일을 리뷰한다)
App --test --record-missing-snapshots       # 없는 스냅숏만 쓰고 그 케이스는 진다(SnapshotMissing)
App --test --test-filter=[slow]             # 태그로(빼기는 -[slow])
App --test --test-shard=1/4                 # 이름 해시로 나눈 4 몫 가운데 1 번(CTest SW_UNIT_TEST_SHARDS)
App --test --test-run-disabled              # 꺼 둔 케이스도
```

- 키트 · 게임 케이스는 그 키트를 올린 프로세스에서만 등록된다. 함께 짓는 게임마다(`SW_GAMES`, Release) `UnitTests.<게임>` 이 그 게임으로 떠서 싣는 폴더의 케이스만 돈다(`--test-source=폴더,폴더`). 어느 게임도 쓰지 않는 키트(`SW_BUILD_UNUSED_KITS`, Release)는 ctest `UnitTests` 에 든다. Debug 는 활성 게임이 싣지 않는 키트를 짓지 않으니 키트를 고친 묶음은 Release 가 확인한다.
- 온라인 키트는 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release)가 돌린다.
- 에디터 케이스(경로에 `/Editor/`)는 `App --test` 에 없다. `App --test-editor -unattended`(ctest `UnitTests.Editor`, nogpu)가 창 · GPU 없이 에디터 인스턴스를 세워 돈다. 에디터 화면(활성 씬 · 프레임)이 있어야 하는 케이스만 태그 `editorApp` 을 붙인다 — `Scenario.editor` 의 `editor.unitTests` 가 돈다.

## 하네스

- 네트워크: Core 엔드포인트는 `LoopbackCluster`, 온라인 키트 끝단은 `OnlineHostTestUtil.h`(`OnlineTestFixture` — 호스트 + 클라이언트, 키트 서버 쪽은 같은 프로토콜 항목의 가짜).
- 그 밖: `AnimationTestUtil` · `RHIFakeDevice` · `FakeFontRasterizer`, 자식 프로세스는 `Core/Process` · 크래시 보고.
- 화면이 있어야 하는 것은 시나리오가 맡는다: `UIPreviewViewModel`(`editor/uipreview` · `uipreviewdesigner`), 섀도 복사본의 성공 길(핫 리로드 시나리오), 화면이 뜨는 게임 흐름(`loadingscreen`), `Editor/Common` · `Editor/Panels` 의 그리기 코드.

## 리플렉션 파서

`ReflectionParser` 는 Engine 을 링크하지 못해 위 틀 밖이다. `Test/ReflectionParser/Fixtures/<이름>.h` 하나가 `ReflectionParser.<이름>` 하나이고(`ctest -L nogpu`), 실제 실행 파일을 빌드와 같은 인자로 돌려 헤더 맨 앞의 기대 줄(`// EXPECT-ERROR:` · `EXPECT-GEN:` · `EXPECT-NOT-GEN:` · `EXPECT-TYPE:`)과 대조한다(형식은 `RunParserFixture.cmake`). 잘못된 입력을 일부러 담으므로 린트 범위(`Source/`) 밖에 둔다.
