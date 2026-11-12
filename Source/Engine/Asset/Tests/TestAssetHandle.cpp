#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryTag.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "Engine/Asset/AssetHandle.h"

using namespace sw;

namespace
{
    /** @brief 단계 본문을 대본대로 실행하는 로드 작업입니다. 부른 횟수를 세어 "단계는 한 번만" 을 봅니다. */
    class FakeAssetLoadOperation final : public AssetLoadOperation
    {
    public:
        using AssetType = int32;

        // 대본 — 테스트가 직접 채운다.
        vector<expected<AssetCompletionStep>> _listBeginStep; ///< 걸음마다 낼 결과(목록이 끝나면 `Done`)
        vector<expected<AssetCompletionStep>> _listPollStep;
        shared_ptr<int32>                     _pAsset;
        string                                _loadError;
        int32*                                _pDestroyCount;
        atomic<bool>*                         _pLoadEntered;
        atomic<bool>*                         _pLoadRelease;
        uint32                                _loadCount;
        uint32                                _beginCount;
        uint32                                _pollCount;
        uint32                                _awaitingCount;
        uint32                                _submittedCount;
        uint8                                 _bHasCompletion     : 1;
        uint8                                 _bUseBaseCompletion : 1;
        uint8                                 _bLastBeginBlocking : 1;
        uint8                                 _bLastPollBlocking  : 1;

        explicit FakeAssetLoadOperation( string_view path = "fake/path" )
            : AssetLoadOperation{ "Fake", path, MemoryTag::EngineMisc }
            , _listBeginStep{}
            , _listPollStep{}
            , _pAsset{}
            , _loadError{}
            , _pDestroyCount{ nullptr }
            , _pLoadEntered{ nullptr }
            , _pLoadRelease{ nullptr }
            , _loadCount{ 0 }
            , _beginCount{ 0 }
            , _pollCount{ 0 }
            , _awaitingCount{ 0 }
            , _submittedCount{ 0 }
            , _bHasCompletion{ SW_FALSE }
            , _bUseBaseCompletion{ SW_FALSE }
            , _bLastBeginBlocking{ SW_FALSE }
            , _bLastPollBlocking{ SW_FALSE }
        {
        }
        ~FakeAssetLoadOperation() override
        {
            if ( _pDestroyCount != nullptr )
                ++( *_pDestroyCount );
        }

        [[nodiscard]] shared_ptr<int32> getAsset() const { return _pAsset; }

        void               exposeMarkReady() { markReadyWithoutLoad(); }
        void               exposeMarkFailed( string_view error ) { markFailedWithoutLoad( string( error ) ); }
        [[nodiscard]] bool exposeIsUploading() const { return isUploading(); }
        [[nodiscard]] bool exposeIsAwaitingCompletion() const { return isAwaitingCompletion(); }

    protected:
        expected<void> runLoad() override
        {
            ++_loadCount;
            if ( _pLoadEntered != nullptr )
                _pLoadEntered->store( true );
            while ( _pLoadRelease != nullptr && _pLoadRelease->load() == false )
            {
                ThreadUtil::yield();
            }
            if ( _loadError.empty() == false )
                return sw::unexpected( Error{ ErrorKind::ExternalFailure, "fake load failed", _loadError } ); // 글은 "fake load failed (<이유>)"
            return {};
        }
        bool                          hasCompletion() const override { return _bHasCompletion != SW_FALSE; }
        expected<AssetCompletionStep> beginCompletion( bool bBlocking ) override
        {
            if ( _bUseBaseCompletion != SW_FALSE )
                return AssetLoadOperation::beginCompletion( bBlocking );
            _bLastBeginBlocking = bBlocking ? SW_TRUE : SW_FALSE;
            return takeStep( _listBeginStep, _beginCount );
        }
        expected<AssetCompletionStep> pollCompletion( bool bBlocking ) override
        {
            _bLastPollBlocking = bBlocking ? SW_TRUE : SW_FALSE;
            return takeStep( _listPollStep, _pollCount );
        }
        /** @brief 목록의 다음 결과입니다. 목록이 끝났으면 `Done` 입니다. */
        static expected<AssetCompletionStep> takeStep( const vector<expected<AssetCompletionStep>>& listStep, uint32& inoutCount )
        {
            const uint32 index = inoutCount++;
            if ( index < listStep.size() )
                return listStep[index];
            return AssetCompletionStep::Done;
        }
        void onAwaitingCompletion() override { ++_awaitingCount; }
        void onUploadSubmitted() override { ++_submittedCount; }
    };

    using FakeHandle = AssetHandle<FakeAssetLoadOperation>;

    struct AssetHandleTestUtil
    {
        static shared_ptr<FakeAssetLoadOperation> makeOperation( string_view path = "fake/path" ) { return sw::make_shared<FakeAssetLoadOperation>( path ); }

        /** @brief 완료 단계가 있고 로드까지 끝난(완료 대기) 작업입니다. */
        static shared_ptr<FakeAssetLoadOperation> makeLoadedWithCompletion()
        {
            shared_ptr<FakeAssetLoadOperation> pOperation = makeOperation();
            pOperation->_bHasCompletion                   = SW_TRUE;
            pOperation->runLoadIfUnclaimed();
            return pOperation;
        }

        /** @brief 이미 끝난 로드 작업(성공 · 실패)입니다. */
        static shared_ptr<FakeAssetLoadOperation> makeReady( string_view path = "fake/ready" )
        {
            shared_ptr<FakeAssetLoadOperation> pOperation = makeOperation( path );
            pOperation->exposeMarkReady();
            return pOperation;
        }
        static shared_ptr<FakeAssetLoadOperation> makeFailed( string_view path, string_view error )
        {
            shared_ptr<FakeAssetLoadOperation> pOperation = makeOperation( path );
            pOperation->exposeMarkFailed( error );
            return pOperation;
        }
    };
    using Util = AssetHandleTestUtil;
} // namespace

/** @brief [AssetHandleTest] 요청 없는 핸들은 상태가 없다 — 묻는 함수는 모두 거짓, 에셋은 없다 */
SW_TEST_CASE( AssetHandleTest, EmptyHandleHasNoRequest )
{
    const FakeHandle handle;
    SW_EXPECT_FALSE( handle.isValid() );
    SW_EXPECT_FALSE( handle.isReady() );
    SW_EXPECT_FALSE( handle.isLoading() );
    SW_EXPECT_FALSE( handle.hasFailed() );
    SW_EXPECT_NULL( handle.get() );
    SW_EXPECT_NULL( handle.getOperation() );
}

/** @brief [AssetHandleTest] 로드 전에는 읽는 중이고 에셋을 주지 않는다. 경로와 종류 이름은 만들 때 값이다 */
SW_TEST_CASE( AssetHandleTest, FreshRequestIsLoading )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation( "game/mesh.mesh" );
    pOperation->_pAsset                           = sw::make_shared<int32>( 7 );
    const FakeHandle handle{ pOperation };
    SW_EXPECT_TRUE( handle.isValid() );
    SW_EXPECT_TRUE( handle.isLoading() );
    SW_EXPECT_FALSE( handle.isReady() );
    SW_EXPECT_EQUAL( AssetLoadState::Loading, handle.getState() );
    SW_EXPECT_NULL( handle.get() ); // 준비되기 전에는 값이 있어도 내주지 않는다
    SW_EXPECT_TRUE( handle.getError().empty() );
    SW_EXPECT_TRUE( handle.getPath() == "game/mesh.mesh" );
    SW_EXPECT_TRUE( string_view( pOperation->getKindName() ) == "Fake" );
}

/** @brief [AssetHandleTest] 기다리면 이 스레드가 로드를 집어 실행하고 에셋을 준다. 로드는 한 번뿐이다 */
SW_TEST_CASE( AssetHandleTest, WaitRunsTheLoadOnce )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation();
    pOperation->_pAsset                           = sw::make_shared<int32>( 7 );
    const FakeHandle        handle{ pOperation };
    const shared_ptr<int32> pAsset = handle.wait();
    SW_REQUIRE_NOT_NULL( pAsset );
    SW_EXPECT_EQUAL( 7, *pAsset );
    SW_EXPECT_TRUE( handle.isReady() );
    SW_EXPECT_FALSE( handle.isLoading() );
    SW_EXPECT_EQUAL( 1u, pOperation->_loadCount );

    SW_EXPECT_NOT_NULL( handle.wait() );
    pOperation->runLoadIfUnclaimed();
    SW_EXPECT_EQUAL( 1u, pOperation->_loadCount );
}

/** @brief [AssetHandleTest] 로드가 실패하면 Failed 와 이유이고 에셋은 없다. 완료 단계로 가지 않는다 */
SW_TEST_CASE( AssetHandleTest, LoadFailureCarriesTheReason )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation();
    pOperation->_loadError                        = "disk read failed";
    pOperation->_pAsset                           = sw::make_shared<int32>( 7 );
    pOperation->_bHasCompletion                   = SW_TRUE;
    const FakeHandle handle{ pOperation };
    SW_EXPECT_NULL( handle.wait() );
    SW_EXPECT_TRUE( handle.hasFailed() );
    SW_EXPECT_EQUAL( AssetLoadState::Failed, handle.getState() );
    SW_EXPECT_TRUE( handle.getError() == "fake load failed (disk read failed)" );
    SW_EXPECT_NULL( handle.get() );
    SW_EXPECT_EQUAL( 0u, pOperation->_beginCount );
    SW_EXPECT_EQUAL( 0u, pOperation->_awaitingCount );
}

/** @brief [AssetHandleTest] 단계 없이 끝낸 작업(캐시 적중 · 틀린 요청)은 본문을 실행하지 않는다 — 시작 · 집기 · 기다림 모두 그대로다 */
SW_TEST_CASE( AssetHandleTest, MarkedOperationsNeverRunTheLoad )
{
    shared_ptr<FakeAssetLoadOperation> pReady = Util::makeReady();
    pReady->_pAsset                           = sw::make_shared<int32>( 1 );
    AssetLoadOperation::startLoad( pReady, AssetLoadStart::OnCallingThread );
    AssetLoadOperation::startLoad( pReady, AssetLoadStart::Async );
    pReady->runLoadIfUnclaimed();
    pReady->wait();
    SW_EXPECT_EQUAL( 0u, pReady->_loadCount );
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pReady->getState() );
    SW_EXPECT_TRUE( pReady->getError().empty() );

    shared_ptr<FakeAssetLoadOperation> pFailed = Util::makeFailed( "", "empty path" );
    AssetLoadOperation::startLoad( pFailed, AssetLoadStart::OnCallingThread );
    pFailed->wait();
    SW_EXPECT_EQUAL( 0u, pFailed->_loadCount );
    SW_EXPECT_EQUAL( AssetLoadState::Failed, pFailed->getState() );
    SW_EXPECT_TRUE( pFailed->getError() == "empty path" );
}

/** @brief [AssetHandleTest] 동기 시작은 부른 자리에서 끝낸다(워커로 넘기지 않아 로그가 그 자리에 남는다) */
SW_TEST_CASE( AssetHandleTest, StartOnCallingThreadFinishesInPlace )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation();
    AssetLoadOperation::startLoad( pOperation, AssetLoadStart::OnCallingThread );
    SW_EXPECT_EQUAL( 1u, pOperation->_loadCount );
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pOperation->getState() );
}

/** @brief [AssetHandleTest] 비동기 시작도 같은 로드 작업이다 — 기다리면 끝나 있고, 본문은 한 번이다(워커와 기다리는 쪽 중 먼저 집은 하나) */
SW_TEST_CASE( AssetHandleTest, AsyncStartRunsTheLoadOnce )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation();
    AssetLoadOperation::startLoad( pOperation, AssetLoadStart::Async );
    pOperation->wait();
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pOperation->getState() );
    SW_EXPECT_EQUAL( 1u, pOperation->_loadCount );
}

/** @brief [AssetHandleTest] 완료 단계가 있으면 로드 뒤에도 Loading 이고, 펌프가 한 번 실행하면 Ready 다. 두 번째 펌프는 할 일이 없다 */
SW_TEST_CASE( AssetHandleTest, CompletionRunsOnThePump )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation();
    pOperation->_bHasCompletion                   = SW_TRUE;
    SW_EXPECT_FALSE( pOperation->runCompletionIfPending() ); // 로드 전에는 없다

    pOperation->runLoadIfUnclaimed();
    SW_EXPECT_EQUAL( AssetLoadState::Loading, pOperation->getState() );
    SW_EXPECT_TRUE( pOperation->exposeIsAwaitingCompletion() );
    SW_EXPECT_EQUAL( 1u, pOperation->_awaitingCount );

    SW_EXPECT_TRUE( pOperation->runCompletionIfPending() );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pOperation->_bLastBeginBlocking ) );
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pOperation->getState() );
    SW_EXPECT_FALSE( pOperation->exposeIsAwaitingCompletion() );

    SW_EXPECT_FALSE( pOperation->runCompletionIfPending() );
    SW_EXPECT_EQUAL( 1u, pOperation->_beginCount );
}

/** @brief [AssetHandleTest] 기다리는 쪽은 완료까지 그 자리에서 막고 실행한다 — 본문은 막아도 된다는 표시를 받는다 */
SW_TEST_CASE( AssetHandleTest, WaitRunsTheCompletionBlocking )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation();
    pOperation->_bHasCompletion                   = SW_TRUE;
    pOperation->wait();
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pOperation->getState() );
    SW_EXPECT_EQUAL( 1u, pOperation->_loadCount );
    SW_EXPECT_EQUAL( 1u, pOperation->_beginCount );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pOperation->_bLastBeginBlocking ) );
}

/** @brief [AssetHandleTest] 완료가 실패하면 Failed 와 이유다 */
SW_TEST_CASE( AssetHandleTest, CompletionFailureCarriesTheReason )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeLoadedWithCompletion();
    pOperation->_listBeginStep.push_back( sw::unexpected( Error{ ErrorKind::ExternalFailure, "upload failed" } ) );
    SW_EXPECT_TRUE( pOperation->runCompletionIfPending() );
    SW_EXPECT_EQUAL( AssetLoadState::Failed, pOperation->getState() );
    SW_EXPECT_TRUE( pOperation->getError() == "upload failed" );
}

/** @brief [AssetHandleTest] 완료가 있다면서 본문을 덮어쓰지 않은 종류는 실패하고 이유에 종류와 경로가 있다(조용히 Ready 가 되지 않는다) */
SW_TEST_CASE( AssetHandleTest, MissingCompletionBodyFails )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation( "game/x" );
    pOperation->_bHasCompletion                   = SW_TRUE;
    pOperation->_bUseBaseCompletion               = SW_TRUE;
    pOperation->wait();
    SW_EXPECT_EQUAL( AssetLoadState::Failed, pOperation->getState() );
    SW_EXPECT_TRUE( pOperation->getError().find( "Fake" ) != string::npos );
    SW_EXPECT_TRUE( pOperation->getError().find( "game/x" ) != string::npos );
}

/** @brief [AssetHandleTest] 지금 낼 수 없으면(Busy) 완료 대기로 돌아가 다시 알리고, 다음 펌프가 다시 낸다 */
SW_TEST_CASE( AssetHandleTest, BusyReturnsToAwaitingCompletion )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeLoadedWithCompletion();
    pOperation->_listBeginStep.push_back( AssetCompletionStep::Busy );
    SW_EXPECT_TRUE( pOperation->runCompletionIfPending() );
    SW_EXPECT_EQUAL( AssetLoadState::Loading, pOperation->getState() );
    SW_EXPECT_TRUE( pOperation->exposeIsAwaitingCompletion() );
    SW_EXPECT_EQUAL( 2u, pOperation->_awaitingCount );

    SW_EXPECT_TRUE( pOperation->runCompletionIfPending() );
    SW_EXPECT_EQUAL( 2u, pOperation->_beginCount );
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pOperation->getState() );
}

/** @brief [AssetHandleTest] 일을 내면(Submitted) 업로드 중이고, 끝남 확인은 펌프가 한 걸음씩 본다. 아직이면 업로드 중으로 남아 다시 알린다 */
SW_TEST_CASE( AssetHandleTest, SubmittedIsPolledUntilDone )
{
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeLoadedWithCompletion();
    pOperation->_listBeginStep.push_back( AssetCompletionStep::Submitted );
    pOperation->_listPollStep.push_back( AssetCompletionStep::Submitted );
    SW_EXPECT_FALSE( pOperation->runUploadPollIfPending() ); // 내기 전에는 볼 것이 없다

    SW_EXPECT_TRUE( pOperation->runCompletionIfPending() );
    SW_EXPECT_EQUAL( AssetLoadState::Loading, pOperation->getState() );
    SW_EXPECT_TRUE( pOperation->exposeIsUploading() );
    SW_EXPECT_EQUAL( 1u, pOperation->_submittedCount );
    SW_EXPECT_FALSE( pOperation->runCompletionIfPending() ); // 완료 대기가 아니다

    SW_EXPECT_TRUE( pOperation->runUploadPollIfPending() );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pOperation->_bLastPollBlocking ) );
    SW_EXPECT_TRUE( pOperation->exposeIsUploading() );
    SW_EXPECT_EQUAL( 2u, pOperation->_submittedCount );

    SW_EXPECT_TRUE( pOperation->runUploadPollIfPending() );
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pOperation->getState() );
    SW_EXPECT_FALSE( pOperation->exposeIsUploading() );
    SW_EXPECT_FALSE( pOperation->runUploadPollIfPending() );
    SW_EXPECT_EQUAL( 2u, pOperation->_pollCount );
}

/** @brief [AssetHandleTest] 업로드 중인 로드를 기다리면 끝남 확인을 집어 막고 본다. 확인이 실패면 Failed 다 */
SW_TEST_CASE( AssetHandleTest, WaitPicksUpAnUploadInProgress )
{
    shared_ptr<FakeAssetLoadOperation> pDone = Util::makeLoadedWithCompletion();
    pDone->_listBeginStep.push_back( AssetCompletionStep::Submitted );
    SW_REQUIRE_TRUE( pDone->runCompletionIfPending() );
    pDone->wait();
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pDone->getState() );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pDone->_bLastPollBlocking ) );

    shared_ptr<FakeAssetLoadOperation> pFailed = Util::makeLoadedWithCompletion();
    pFailed->_listBeginStep.push_back( AssetCompletionStep::Submitted );
    pFailed->_listPollStep.push_back( sw::unexpected( Error{ ErrorKind::ExternalFailure, "device lost" } ) );
    SW_REQUIRE_TRUE( pFailed->runCompletionIfPending() );
    pFailed->wait();
    SW_EXPECT_EQUAL( AssetLoadState::Failed, pFailed->getState() );
    SW_EXPECT_TRUE( pFailed->getError() == "device lost" );
}

/** @brief [AssetHandleTest] 다른 스레드가 로드 중이면 집지 않고 끝나기를 잠들어 기다린다 — 본문은 그 스레드의 한 번뿐이다 */
SW_TEST_CASE( AssetHandleTest, WaitSleepsWhileAnotherThreadLoads )
{
    atomic<bool> bEntered{ false };
    atomic<bool> bRelease{ false };
    atomic<bool> bWaited{ false };

    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeOperation();
    pOperation->_pLoadEntered                     = &bEntered;
    pOperation->_pLoadRelease                     = &bRelease;

    thread loader{ ThreadTestAccess::getKey(), [&]
    {
        pOperation->runLoadIfUnclaimed();
    } };
    while ( bEntered.load() == false )
    {
        ThreadUtil::yield();
    }
    thread waiter{ ThreadTestAccess::getKey(), [&]
    {
        pOperation->wait();
        bWaited.store( true );
    } };
    SW_EXPECT_FALSE( bWaited.load() );
    bRelease.store( true );
    loader.join();
    waiter.join();

    SW_EXPECT_TRUE( bWaited.load() );
    SW_EXPECT_EQUAL( AssetLoadState::Ready, pOperation->getState() );
    SW_EXPECT_EQUAL( 1u, pOperation->_loadCount );
}

/** @brief [AssetHandleTest] 핸들을 복사하면 같은 로드를 나눠 가진다. 마지막 핸들이 놓일 때까지 작업과 에셋이 산다 */
SW_TEST_CASE( AssetHandleTest, HandlesShareTheOperationLifetime )
{
    int32           destroyCount{ 0 };
    weak_ptr<int32> pWeakAsset;
    {
        shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeReady();
        pOperation->_pDestroyCount                    = &destroyCount;
        pOperation->_pAsset                           = sw::make_shared<int32>( 3 );
        pWeakAsset                                    = pOperation->_pAsset;

        FakeHandle first{ std::move( pOperation ) };
        FakeHandle second = first;
        SW_EXPECT_TRUE( first.getOperation() == second.getOperation() );

        first.reset();
        SW_EXPECT_FALSE( first.isValid() );
        SW_EXPECT_FALSE( first.isReady() );
        SW_EXPECT_TRUE( second.isReady() );
        SW_EXPECT_EQUAL( 0, destroyCount );
        SW_EXPECT_FALSE( pWeakAsset.expired() );

        second.reset();
        SW_EXPECT_EQUAL( 1, destroyCount );
        SW_EXPECT_TRUE( pWeakAsset.expired() );
    }
}

/** @brief [AssetReadySetTest] 빈 집합은 끝났고 준비됐다 — 로딩 화면이 기다릴 것이 없다 */
SW_TEST_CASE( AssetReadySetTest, EmptySetIsReady )
{
    AssetReadySet readySet;
    SW_EXPECT_TRUE( readySet.isComplete() );
    SW_EXPECT_TRUE( readySet.isReady() );
    SW_EXPECT_FALSE( readySet.hasFailed() );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( readySet.countLoading() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( readySet.countReady() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( readySet.getCount() ) );
}

/** @brief [AssetReadySetTest] 읽는 중이 하나라도 있으면 끝나지 않았고, 끝나면 세는 값이 따라온다 */
SW_TEST_CASE( AssetReadySetTest, CountsFollowTheOperations )
{
    shared_ptr<FakeAssetLoadOperation> pLoading = Util::makeOperation();
    AssetReadySet                      readySet;
    readySet.add( FakeHandle{ Util::makeReady() } );
    readySet.add( FakeHandle{ pLoading } );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( readySet.getCount() ) );
    SW_EXPECT_FALSE( readySet.isComplete() );
    SW_EXPECT_FALSE( readySet.isReady() );
    SW_EXPECT_FALSE( readySet.hasFailed() );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( readySet.countLoading() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( readySet.countReady() ) );

    pLoading->wait();
    SW_EXPECT_TRUE( readySet.isComplete() );
    SW_EXPECT_TRUE( readySet.isReady() );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( readySet.countLoading() ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( readySet.countReady() ) );
}

/** @brief [AssetReadySetTest] 실패가 있으면 끝났지만 준비되지 않았고, 실패 목록은 "종류 경로: 이유" 를 덧붙인다(앞 내용을 지우지 않는다) */
SW_TEST_CASE( AssetReadySetTest, FailuresAreReported )
{
    AssetReadySet readySet;
    readySet.add( FakeHandle{ Util::makeReady() } );
    readySet.add( FakeHandle{ Util::makeFailed( "game/a.mesh", "disk read failed" ) } );
    SW_EXPECT_TRUE( readySet.isComplete() );
    SW_EXPECT_FALSE( readySet.isReady() );
    SW_EXPECT_TRUE( readySet.hasFailed() );

    vector<string> listError;
    listError.emplace_back( "earlier" );
    readySet.appendFailures( listError );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listError.size() ) );
    SW_EXPECT_TRUE( listError[0] == "earlier" );
    SW_EXPECT_TRUE( listError[1] == "Fake game/a.mesh: disk read failed" );
}

/** @brief [AssetReadySetTest] 집합은 작업을 쥐어 살리고, 비우면 놓는다 */
SW_TEST_CASE( AssetReadySetTest, SetKeepsOperationsAliveUntilCleared )
{
    int32                              destroyCount{ 0 };
    shared_ptr<FakeAssetLoadOperation> pOperation = Util::makeReady();
    pOperation->_pDestroyCount                    = &destroyCount;

    AssetReadySet readySet;
    readySet.add( FakeHandle{ std::move( pOperation ) } );
    SW_EXPECT_EQUAL( 0, destroyCount );
    readySet.clear();
    SW_EXPECT_EQUAL( 1, destroyCount );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( readySet.getCount() ) );
    SW_EXPECT_TRUE( readySet.isReady() );
}
