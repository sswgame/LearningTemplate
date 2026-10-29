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

```
App --test                                  # 모두(순서는 섞이고 씨앗이 찍힌다)
App --test --test-filter=LODViewTest.*      # 와일드카드, 쉼표로 여럿, 앞에 - 면 뺀다
App --test --test-seed=2741926653           # 찍힌 순서를 다시
App --test --test-repeat=20                 # 간헐 실패 찾기
App --test --update-snapshots               # 스냅숏을 지금 값으로(바뀐 파일을 리뷰한다)
```

케이스 하나는 이럴 때 실패다: 단언 실패 · 단언이 하나도 없음 · 적지 않은 Warning · Error 로그 · 적었는데 나오지 않은 로그 · 다른 스냅숏. 거르개에 맞는 케이스가 없으면 실행이 실패다. 보고 JSON 은 `Saved/Test/<프로세스>/report.json`(찍힌다).

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
- `timeoutFrames` 는 시작한 뒤 지난 **모든** 프레임(조건 대기 포함)을 센다.
- 이미지는 골든 비교다: 영역을 가로 160 화소 이하로 줄여 `golden/<시나리오>.<이름>.<백엔드>.ppm` 과 견주고, 색 거리 12 이상인 화소가 `tolerance`(기본 1 %)를 넘으면 실패다. 골든은 `-scenario-update-golden` 으로 뜨거나 갱신하고(보고의 `updatedGoldens`), 그림을 보고 커밋한다.
- 로그 게이트: 시나리오 구간의 Warning · Error 는 `<ExpectLog>`(기대) 나 `<AllowLog>`(허용, 사유 필수)가 없으면 실패다. id 없는 줄은 허용할 수 없다 — 소스에 id 를 붙인다.
- 산출물(사진 · 보고)은 `Saved/Automation/<시나리오>/<프로세스>/` 에 쓴다. 무인 실행은 사용자 설정 파일을 읽지도 쓰지도 않고 사양 자동 프리셋을 쓰지 않는다.
- 하위 폴더(예: `rhiswap/` — 네 백엔드가 모두 있어야 한다)는 조건이 붙은 시나리오라 ctest 가 줍지 않는다.

종료 코드: 0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀 · 15 처리하지 않은 로그.

## 옛 틀에서 고친 것

| 옛 문제 | 지금 |
|---|---|
| 소스를 거울로 따르는 `Test/` 트리 · 링크마다 실행 파일 · TestPolicy.json(라벨 · 샤드 · 비용표) | 기능 옆 `Tests/`, `App --test` 하나, 정책 파일 없음 |
| 배포에서만 지는 테스트 · 구성마다 다른 케이스 수 | 배포는 테스트를 짓지 않음, 보고에 돈 케이스 수 |
| 앞 케이스에 기대는 순서 의존 | 매번 섞는 순서 · 씨앗 · `--test-repeat` |
| 표식 없는 경고 · 부분 문자열로 가리기 | 케이스 · 시나리오 단위 로그 게이트, id 로만 선언 |
| 0 건 통과 | 단언 없는 케이스 · 빈 실행은 실패 |
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
