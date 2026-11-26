// PhaseTimingsTest — 단계 시간표(`PhaseTimings`)와 스코프 측정(nogpu). 시간 값은 실제 단조 시계라 크기가 아니라 합침 · 차례만 본다.
#include "pch.h"

#include "Core/Diagnostics/PhaseTimings.h"
#include "Core/Diagnostics/ScopedTimer.h"
#include "Core/TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [PhaseTimingsTest] 같은 이름의 단계는 한 칸으로 합치고, 스코프 측정과 차례 찍기가 같은 표에 든다
 * @details 변이: 합치지 않으면 add 뒤의 값이 앞 값을 덮거나 칸이 둘이 되어 진다.
 */
SW_TEST_CASE( PhaseTimingsTest, SameNameMerges )
{
    PhaseTimings table;
    table.begin();
    table.add( "load", 10 );
    table.add( "load", 5 );
    {
        ScopedPhaseTiming scope{ table, "scoped" };
    }
    table.mark( "marked" );
    SW_EXPECT_EQUAL( int64( 15 ), table.findMicroseconds( "load" ) );
    SW_EXPECT_TRUE( table.findMicroseconds( "scoped" ) >= 0 );
    SW_EXPECT_EQUAL( int64( 0 ), table.findMicroseconds( "missing" ) );
    const string text = table.describe();
    SW_EXPECT_TRUE( text.find( "load" ) < text.find( "scoped" ) && text.find( "scoped" ) < text.find( "marked" ) );
}

/**
 * @brief [PhaseTimingsTest] ScopedTimer 는 몇 번을 읽어도 시작부터 잰다 — 두 번째 읽기가 앞 읽기보다 작지 않다
 * @details 예전에는 읽을 때마다 기준점이 옮겨(게임 타이머 갱신) 두 번째 읽기 · 소멸자 로그가 0 이었다.
 */
SW_TEST_CASE( PhaseTimingsTest, ScopedTimerReadsFromItsStart )
{
    const ScopedTimer timer( "PhaseTimingsTest" );
    const float64     first  = timer.getElapsedSeconds();
    const float64     second = timer.getElapsedSeconds();
    SW_EXPECT_TRUE( first >= 0.0 && second >= first );
}
