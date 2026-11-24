/**
 * @file TestFileWatchService.cpp
 * @brief 파일 감시 창구가 감시자를 루트마다 하나씩 나눠 쓰고 한 프레임의 변경을 모아 나눠 주는지 — OS 감시자 없이(가짜 감시자) 확인한다(nogpu).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/FileWatch/FileWatchService.h"

using namespace sw;

namespace
{
    /** @brief OS 를 쓰지 않는 감시자입니다. 테스트가 변경을 직접 넣는다. */
    class FakeWatcher final : public FileWatcher
    {
    public:
        explicit FakeWatcher( vector<FakeWatcher*>& listOpen )
            : _listOpen{ listOpen }
        {
        }
        ~FakeWatcher() override
        {
            for ( size_t index = 0; index < _listOpen.size(); ++index )
            {
                if ( _listOpen[index] == this )
                    _listOpen[index] = nullptr;
            }
        }

        [[nodiscard]] bool startWatching( const AbsolutePath& directoryPath, bool /*bRecursive*/ ) override
        {
            _directoryPath = directoryPath;
            _listOpen.push_back( this );
            return true;
        }
        void stopWatching() override {}
        bool isWatching() const override { return true; }
        void push( FileWatcherAction action, string_view filename ) { pushChange( action, _directoryPath.str(), filename ); }

    private:
        vector<FakeWatcher*>& _listOpen; ///< 테스트가 든 열린 감시자 목록(닫히면 nullptr)
    };

    /** @brief 케이스 폴더 아래 절대 경로입니다(감시자가 가짜라 폴더는 없어도 된다). */
    AbsolutePath makeFolder( string_view relative )
    {
        return test::makeScratchPath( relative );
    }

    /** @brief 받은 사건을 모으는 구독자입니다. */
    struct Receiver
    {
        vector<string>     _listName;
        void               onChanged( const FileChangeEvent& changeEvent ) { _listName.push_back( changeEvent._filename ); }
        FileChangeDelegate makeDelegate() { return SW_DELEGATE_METHOD( FileChangeDelegate, &Receiver::onChanged, this ); }
    };

    /** @brief 가짜 감시자를 만드는 창구와 그것이 연 감시자들입니다. */
    struct ServiceFixture
    {
        vector<FakeWatcher*> _listOpen;
        FileWatchService     _service{ SW_DELEGATE_LAMBDA( FileWatchService::CreateWatcherDelegate, [this]()
            { return unique_ptr<FileWatcher>{ make_unique<FakeWatcher>( _listOpen ) }; } ) };
    };
} // namespace

/**
 * @brief [FileWatchServiceTest] 이미 보는 트리 안의 폴더는 그 감시자를 같이 쓰고, 트리 밖은 감시자를 하나 더 띄운다. 마지막 감시가 빠지면 감시자를 내린다
 * @details 변이: 감시마다 감시자를 띄우면(예전 — 설정 · 데이터 폴더 · 에디터 · 핫 리로드가 저마다) 첫 묶음의 루트 수가 진다.
 */
SW_TEST_CASE( FileWatchServiceTest, RootsAreSharedAndReleased )
{
    ServiceFixture fixture;
    Receiver       receiver;
    Subscription   tree   = fixture._service.watch( makeFolder( "Content" ), FileWatchDepth::Tree, {}, receiver.makeDelegate() );
    Subscription   nested = fixture._service.watch( makeFolder( "Content/common/data" ), FileWatchDepth::Tree, { ".xml" }, receiver.makeDelegate() );
    SW_EXPECT_EQUAL( size_t( 1 ), fixture._service.getRootCount() );
    Subscription other = fixture._service.watch( makeFolder( "Config" ), FileWatchDepth::Tree, { ".json" }, receiver.makeDelegate() );
    SW_EXPECT_EQUAL( size_t( 2 ), fixture._service.getRootCount() );

    other.reset();
    SW_EXPECT_EQUAL( size_t( 1 ), fixture._service.getRootCount() );
    tree.reset(); // 안쪽 감시가 남아 루트는 산다
    SW_EXPECT_EQUAL( size_t( 1 ), fixture._service.getRootCount() );
    nested.reset();
    SW_EXPECT_EQUAL( size_t( 0 ), fixture._service.getRootCount() );
    SW_EXPECT_NULL( fixture._listOpen[0] );
}

/** @brief [FileWatchServiceTest] 접두사 · 확장자 · 깊이가 맞는 구독자만 받고, 한 프레임에 같은 파일의 같은 동작은 한 번이다(지움 → 만듦은 차례 그대로) */
SW_TEST_CASE( FileWatchServiceTest, OneBatchPerFrameReachesOnlyMatchingWatches )
{
    ServiceFixture     fixture;
    Receiver           data;
    Receiver           rootOnly;
    const Subscription treeWatch   = fixture._service.watch( makeFolder( "Content" ), FileWatchDepth::Tree, {}, FileChangeDelegate{} );
    const Subscription dataWatch   = fixture._service.watch( makeFolder( "Content/common/data" ), FileWatchDepth::Tree, { ".xml" }, data.makeDelegate() );
    const Subscription folderWatch = fixture._service.watch( makeFolder( "Content" ), FileWatchDepth::Folder, {}, rootOnly.makeDelegate() );
    SW_REQUIRE_EQUAL( size_t( 1 ), fixture._listOpen.size() );
    FakeWatcher& watcher = *fixture._listOpen[0];
    watcher.push( FileWatcherAction::Modified, "common/data/a.xml" );
    watcher.push( FileWatcherAction::Modified, "common/data/b.xml" );
    watcher.push( FileWatcherAction::Modified, "common/data/a.xml" ); // 다른 파일이 끼어도 같은 동작은 한 번
    watcher.push( FileWatcherAction::Modified, "common/data/a.png" ); // 확장자 밖
    watcher.push( FileWatcherAction::Removed, "common/data/a.xml" );
    watcher.push( FileWatcherAction::Added, "common/data/a.xml" );
    watcher.push( FileWatcherAction::Modified, "top.txt" );
    fixture._service.update();
    SW_EXPECT_TRUE( data._listName == ( vector<string>{ "common/data/a.xml", "common/data/b.xml", "common/data/a.xml", "common/data/a.xml" } ) );
    SW_EXPECT_TRUE( rootOnly._listName == vector<string>{ "top.txt" } );
    fixture._service.update(); // 비운 뒤에는 다시 오지 않는다
    SW_EXPECT_EQUAL( size_t( 4 ), data._listName.size() );
}
