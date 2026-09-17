/**
 * @file TestFileWatcher.cpp
 * @brief 세 플랫폼 워처가 공유하는 큐 규칙 — 상한 · 오버플로 리스캔 · 연속 중복 접기.
 * @details 이 로직은 예전에 Windows · Linux · macOS 구현에 **세 벌**로 적혀 있었고 테스트가 없었다.
 *          그래서 macOS 만 연속 중복 접기가 빠진 채로 남아 있었는데, 이 저장소는 macOS 를 빌드하지
 *          않으므로 드러날 길이 없었다. `IFileWatcher` 로 모은 뒤부터는 **플랫폼과 무관하게** 검사할 수
 *          있다 — 여기서 OS 를 전혀 건드리지 않고 규칙만 확인한다.
 */
#include "pch.h"

#include "Core/File/IFileWatcher.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief OS 를 쓰지 않는 시험용 워처 — 공유 큐 규칙만 드러낸다. */
    class StubFileWatcher final : public sw::IFileWatcher
    {
    public:
        /** @brief 감시 루트를 정해 둡니다. 합성 리스캔 이벤트가 이 값을 답니다. */
        explicit StubFileWatcher( sw::string_view watchRoot ) { _directoryPath = sw::string{ watchRoot }; }

        bool startWatching( sw::string_view, bool ) override { return true; }
        void stopWatching() override {}
        bool isWatching() const override { return true; }

        /** @brief 보호된 `pushChange` 를 테스트에서 부를 수 있게 엽니다. */
        void push( sw::FileWatcherAction action, sw::string_view filename )
        {
            pushChange( action, _directoryPath, filename );
        }
    };
} // namespace

/**
 * @brief [FileWatcherTest] 같은 파일에 같은 동작이 **연달아** 오면 하나로 접는다
 * @details 저장 한 번에 OS 가 변경 알림을 둘씩 준다(리눅스의 IN_MODIFY + IN_CLOSE_WRITE, 윈도우의
 *          LAST_WRITE + SIZE). 접지 않으면 리로드가 두 번 돌고 큐도 그만큼 빨리 찬다.
 *          **macOS 구현에만 이 규칙이 없었다.**
 */
SW_TEST_CASE( FileWatcherTest, ConsecutiveDuplicatesCollapse )
{
    StubFileWatcher watcher{ "root" };

    watcher.push( sw::FileWatcherAction::Modified, "a.txt" );
    watcher.push( sw::FileWatcherAction::Modified, "a.txt" );
    watcher.push( sw::FileWatcherAction::Modified, "a.txt" );

    sw::vector<sw::FileChangeEvent> listEvent;
    SW_EXPECT_EQUAL( 1u, watcher.pollEvents( listEvent ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, listEvent.size() );
}

/**
 * @brief [FileWatcherTest] 연달아가 아니면 접지 않는다
 * @details 접는 기준은 "직전 것과 같은가" 다 — 사이에 다른 파일이 끼면 각각 살아 있어야 한다.
 *          그러지 않으면 A→B→A 편집에서 마지막 A 를 잃는다.
 */
SW_TEST_CASE( FileWatcherTest, NonConsecutiveDuplicatesAreKept )
{
    StubFileWatcher watcher{ "root" };

    watcher.push( sw::FileWatcherAction::Modified, "a.txt" );
    watcher.push( sw::FileWatcherAction::Modified, "b.txt" );
    watcher.push( sw::FileWatcherAction::Modified, "a.txt" );

    sw::vector<sw::FileChangeEvent> listEvent;
    SW_EXPECT_EQUAL( 3u, watcher.pollEvents( listEvent ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, listEvent.size() );

    // 같은 파일이라도 동작이 다르면 접지 않는다.
    watcher.push( sw::FileWatcherAction::Added, "c.txt" );
    watcher.push( sw::FileWatcherAction::Removed, "c.txt" );

    listEvent.clear();
    SW_EXPECT_EQUAL( 2u, watcher.pollEvents( listEvent ) );
}

/**
 * @brief [FileWatcherTest] 큐가 상한에 걸리면 **합성 리스캔 하나**로 접는다
 * @details 폴링이 밀리는 동안(대량 임포트·브랜치 전환) 큐가 끝없이 자라면 안 된다. 상한을 넘으면
 *          개별 변경을 버리고, 다음 폴링이 "전부 다시 봐라" 하나를 덧붙인다 —
 *          **파일 이름이 빈 `Modified`** 가 그 약속이고 소비자가 그렇게 읽는다.
 */
SW_TEST_CASE( FileWatcherTest, QueueCapCollapsesIntoSingleRescan )
{
    StubFileWatcher watcher{ "root" };

    // 상한보다 넉넉히 많이, 그리고 연속 중복으로 접히지 않게 서로 다른 이름으로 넣는다.
    constexpr uint32 kPushCount = 5000;
    for ( uint32 pushIndex = 0; pushIndex < kPushCount; ++pushIndex )
    {
        watcher.push( sw::FileWatcherAction::Modified, sw::string{ "file" } + sw::to_string( pushIndex ) );
    }

    sw::vector<sw::FileChangeEvent> listEvent;
    const uint32                    polled = watcher.pollEvents( listEvent );

    // 상한만큼 담기고, 버린 것을 알리는 합성 리스캔 하나가 더 붙는다.
    SW_EXPECT_TRUE_MSG( polled < kPushCount, "상한이 걸리지 않아 큐가 끝없이 자랐다" );
    SW_EXPECT_EQUAL( static_cast<uint32>( listEvent.size() ), polled );

    const sw::FileChangeEvent& rescanEvent = listEvent.back();
    SW_EXPECT_TRUE_MSG( rescanEvent._filename.empty(), "합성 리스캔의 파일 이름이 비어 있지 않다 — 소비자가 일반 변경으로 읽는다" );
    SW_EXPECT_TRUE( rescanEvent._action == sw::FileWatcherAction::Modified );
    SW_EXPECT_STREQ( "root", rescanEvent._directory.c_str() );

    // 리스캔은 한 번만 알린다 — 다음 폴링에는 남아 있지 않다.
    listEvent.clear();
    SW_EXPECT_EQUAL( 0u, watcher.pollEvents( listEvent ) );
}
