# Test — 단위 테스트와 시나리오

> 질문: 테스트와 시나리오를 어디에 쓰고 어떻게 돌리는가?
> 독자: 기능을 고치거나 더하는 사람과 에이전트.
> 결론: GPU 없이 되는 확인은 `Test/<실행 파일>/` 의 C++ 케이스(`SW_TEST_CASE`), 실행 중인 엔진이 있어야 하는 확인은 `Content/**/automation/*.scenario.xml` 시나리오다. 둘 다 `ctest` 하나로 돈다.

## 무엇을 어디에

| 확인할 것 | 쓰는 곳 | 도는 방법 |
|---|---|---|
| 순수 식 · 형식 · 파일 형식 · 등록표(GPU · 창 없음) | `Test/CoreTest`(Core 만) · `Test/EngineTest`(Engine DLL) | `ctest -L nogpu` |
| 화면 · 입력 · 씬 · 에디터 동작(엔진이 돌아야 함) | `Content/<도메인>/automation/<이름>.scenario.xml` | `App -scenario=<리소스 경로> -unattended`(종료 코드 0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀) |

- 실행 파일은 **링크하는 것**으로 나뉜다. 새 테스트는 필요한 것을 이미 링크하는 실행 파일에 넣고, 없으면 `Test/<이름>/CMakeLists.txt` 에 `sw_addTestExecutable(<이름> LINK … LABELS …)` 한 줄로 하나 더 만든다(목록이 없다 — 폴더를 훑는다).
- 파일은 소스 경로를 따른다: `Source/Engine/Graphics/LOD/LODView.h` → `Test/EngineTest/Graphics/LOD/TestLODView.cpp`.

## 케이스 쓰기

```cpp
#include "Core/String/Base64Util.h"

#include "TestFramework/TestFramework.h"

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

- `SW_EXPECT_TRUE` · `SW_EXPECT_FALSE` · `SW_EXPECT_EQUAL` · `SW_EXPECT_NEAR` 는 실패를 적고 이어 간다. `SW_REQUIRE_TRUE` · `SW_REQUIRE_EQUAL` 은 실패하면 케이스 함수에서 `return` 한다(도우미 함수 안에서는 쓰지 않는다).
- 실패는 `파일(줄): error: …` 로 찍혀 IDE 가 그 줄로 간다. `SW_EXPECT_EQUAL` 은 숫자 · 문자열이면 두 값을 쓴다.
- 이름은 `<스위트>.<케이스>` 이고 실행 순서는 이름 순이다(등록 순서는 링크 순서라 쓰지 않는다).

## 돌리기

```
cmake --build --preset Ninja-Debug --target CoreTest EngineTest
cd build/Ninja-Debug && ctest --output-on-failure
Bin/EngineTest.exe --filter=LODViewTest.*        # 와일드카드, 쉼표로 여럿, 앞에 - 면 뺀다
Bin/CoreTest.exe --list
```

종료 코드: 0 통과 · 1 실패 · 2 거르개에 맞는 케이스 없음(빈 실행을 통과로 보지 않는다) · 3 인자 오류.

## 시나리오

형식은 `Source/Engine/Automation/Scenario/AutomationScenario.h` 와 단계 등록(`SW_AUTOMATION_STEP`), 감시 값 등록(`SW_AUTOMATION_WATCH`)이 정본이다.
시간 축은 `<At frame="N">` 이고, 단계(`DevCommand` · `Variable` · `Tap` · `Press` · `Release` · `Expect watch=…` · `WaitUntil` …)를 그 프레임에 둔다.
`ctest -L hostgpu` 가 `Content/` 아래 시나리오마다 App 을 띄운다(창과 GPU 가 있는 PC에서).

## 왜 이 모양인가

- 언리얼은 단위 테스트(Automation Spec · `IMPLEMENT_SIMPLE_AUTOMATION_TEST`)와 기능 테스트(Functional Test 맵 · Gauntlet)를 나눈다. 여기서도 GPU 없이 되는 확인은 C++ 케이스, 엔진이 돌아야 하는 확인은 시나리오다.
- 테스트 틀을 직접 만든 까닭: 엔진의 문자열 · 서식(`formatstring`) · 표준 출력을 그대로 쓰고, 외부 의존(gtest)을 늘리지 않는다. 명령줄은 gtest 모양(`--gtest_filter`)도 받아 IDE 테스트 탐색기가 부를 수 있다.
