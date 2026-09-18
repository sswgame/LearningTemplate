#include "pch.h"

#include "Engine/Utility/Debug/FrameProfiler.h"

#include "TestFramework/TestFramework.h"

// FrameProfiler — 구간 등록 표의 경계와 누적/보고.
/**
 * @brief [FrameProfilerTest] 구간이 표를 넘쳐도 표 밖을 읽지 않는다
 * @details `registerScope` 는 같은 이름을 찾으려고 `_scopeCount` 까지 선형 탐색한다. 그런데
 *          표가 꽉 찬 뒤에도 `fetch_add` 는 계속 카운터를 올리므로 그 값이 `kMaxScope` 를
 *          넘어간다 — 그러면 다음 탐색이 **고정 배열 밖**을 읽는다. 배열 바로 뒤에 있는 것이
 *          `_scopeCount` 자신이라, 그 비트가 `const utf8*` 로 읽혀 문자열 비교에 들어간다.
 *          같은 파일의 다른 세 순회(`endFrame` · `report` · `reset`)는 모두
 *          `index < kMaxScope` 로 막고 있었는데, 넘침을 만드는 이 함수만 막지 않았다.
 */

SW_TEST_CASE( FrameProfilerTest, ScopeOverflowDoesNotReadPastTable )
{
    sw::FrameProfiler profiler;

    // 이름 포인터를 보관하는 API 다 — 문자열이 옮겨 다니지 않도록 자리를 먼저 잡는다.
    constexpr uint32       kExtra = 4;
    sw::vector<sw::string> listName;
    listName.reserve( sw::FrameProfiler::kMaxScope + kExtra );

    for ( uint32 index = 0; index < sw::FrameProfiler::kMaxScope; ++index )
    {
        listName.push_back( sw::string( "Scope" ) + sw::to_string( index ) );
        SW_ASSERT_EQUAL( index, profiler.registerScope( listName.back().c_str() ) );
    }

    // 표가 꽉 찼다. 새 이름은 슬롯을 받지 못한다 — 측정이 실행을 막으면 안 되므로 조용히 무시한다.
    {
        test::ScopedLogSuppressor suppressor;
        for ( uint32 extraIndex = 0; extraIndex < kExtra; ++extraIndex )
        {
            listName.push_back( sw::string( "Overflow" ) + sw::to_string( extraIndex ) );
            SW_EXPECT_EQUAL( sw::FrameProfiler::kInvalidSlot, profiler.registerScope( listName.back().c_str() ) );
        }
    }

    // 넘친 **뒤에도** 이미 등록된 이름은 제 슬롯을 그대로 찾아야 한다. 예전에는 이 탐색이
    // 배열 밖까지 훑었다.
    SW_EXPECT_EQUAL( uint32( 0 ), profiler.registerScope( listName[0].c_str() ) );
    SW_EXPECT_EQUAL( sw::FrameProfiler::kMaxScope - 1,
                     profiler.registerScope( listName[sw::FrameProfiler::kMaxScope - 1].c_str() ) );
}

/**
 * @brief [FrameProfilerTest] 꺼져 있으면 아무것도 쌓이지 않고, 켜면 프레임 누적이 통계로 접힌다
 */
SW_TEST_CASE( FrameProfilerTest, DisabledCollectsNothingAndEnabledAccumulates )
{
    sw::FrameProfiler profiler;
    const uint32      slot = profiler.registerScope( "Pass" );
    SW_ASSERT_TRUE( slot != sw::FrameProfiler::kInvalidSlot );

    // 꺼진 상태: endFrame 이 아무것도 접지 않는다.
    SW_EXPECT_FALSE( profiler.isEnabled() );
    profiler.addSample( slot, 1000 );
    profiler.beginFrame();
    profiler.endFrame();
    SW_EXPECT_EQUAL( uint64( 0 ), profiler.getFrameCount() );

    profiler.setEnabled( true );
    profiler.beginFrame();
    profiler.addSample( slot, 1000 );
    profiler.addSample( slot, 3000 );
    profiler.endFrame();
    SW_EXPECT_EQUAL( uint64( 1 ), profiler.getFrameCount() );

    profiler.reset();
    SW_EXPECT_EQUAL( uint64( 0 ), profiler.getFrameCount() );
}
