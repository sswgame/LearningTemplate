#include "pch.h"

#include "Engine/Asset/AssetHandle.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/Cook/CookJob.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "AssetHandle" );

    AssetLoadBlock::AssetLoadBlock( const utf8* pKindName, string_view path )
        : _path{ path }
        , _error{}
        , _pKindName{ pKindName }
        , _phase{ kPhaseQueued }
        , _state{ AssetLoadState::Loading }
    {
    }

    AssetLoadBlock::~AssetLoadBlock() = default;

    AssetLoadState AssetLoadBlock::getState() const
    {
        return _state.load( std::memory_order_acquire );
    }

    const string& AssetLoadBlock::getError() const
    {
        static const string s_empty{};
        return getState() == AssetLoadState::Failed ? _error : s_empty;
    }

    void AssetLoadBlock::wait()
    {
        for ( ;; )
        {
            uint32 phase = _phase.load( std::memory_order_acquire );
            if ( phase == kPhaseDone )
                return;
            if ( phase == kPhaseQueued && _phase.compare_exchange_strong( phase, kPhaseLoading, std::memory_order_acq_rel ) )
            {
                executeLoad();
                continue;
            }
            if ( phase == kPhaseAwaitingCompletion && _phase.compare_exchange_strong( phase, kPhaseCompleting, std::memory_order_acq_rel ) )
            {
                executeCompletion( true );
                continue;
            }
            // 펌프가 일을 내 둔 로드다 — 기다리는 쪽이 끝남 확인을 집어 끝날 때까지 기다린다(블로킹 헬퍼: 제출 → 대기 → 끝내기).
            if ( phase == kPhaseUploading && _phase.compare_exchange_strong( phase, kPhaseCompleting, std::memory_order_acq_rel ) )
            {
                executeUploadPoll( true );
                continue;
            }
            // 다른 스레드가 이 단계를 실행 중이다 — 그 스레드는 일하는 중이므로 끝나면 단계 워드를 바꾸고 깨운다.
            const bool bOtherThreadRunning = phase == kPhaseLoading || phase == kPhaseCompleting;
            if ( bOtherThreadRunning )
                Futex::wait( _phase, phase );
        }
    }

    void AssetLoadBlock::runLoadIfUnclaimed()
    {
        uint32 expected = kPhaseQueued;
        if ( _phase.compare_exchange_strong( expected, kPhaseLoading, std::memory_order_acq_rel ) )
            executeLoad();
    }

    bool AssetLoadBlock::runCompletionIfPending()
    {
        uint32 expected = kPhaseAwaitingCompletion;
        if ( _phase.compare_exchange_strong( expected, kPhaseCompleting, std::memory_order_acq_rel ) == false )
            return false;
        executeCompletion( false );
        return true;
    }

    bool AssetLoadBlock::runUploadPollIfPending()
    {
        uint32 expected = kPhaseUploading;
        if ( _phase.compare_exchange_strong( expected, kPhaseCompleting, std::memory_order_acq_rel ) == false )
            return false;
        executeUploadPoll( false );
        return true;
    }

    bool AssetLoadBlock::canLoadOnWorker()
    {
        // 쿠킹 중에는 요청한 스레드(그 작업 범위 안)에서 읽는다 — 워커에서 읽으면 작업의 입력에서 빠지고 쿠킹 세션이 그 읽기를 실패시킨다.
        return engine::areEngineServicesBound() && CookJob::requiresCallingThreadRead() == false;
    }

    void AssetLoadBlock::startLoad( const shared_ptr<AssetLoadBlock>& pBlock, AssetLoadStart start )
    {
        SW_ASSERT( pBlock != nullptr );
        if ( pBlock->_phase.load( std::memory_order_acquire ) != kPhaseQueued )
            return; // 캐시 적중 · 틀린 요청으로 이미 끝난 블록이다
        if ( start == AssetLoadStart::OnCallingThread || canLoadOnWorker() == false )
        {
            // 동기 로드 공용 진입점이다(프리팹 · 머티리얼 · 텍스처 · 메시 · 셰이더의 `OnCallingThread` 요청이 모두 여기를 지난다).
            // 서비스가 묶이기 전(도구 · 테스트)에는 프로파일러가 없어 계측 없이 읽는다.
            if ( engine::areEngineServicesBound() )
            {
                SW_PROFILE_SCOPE( "GT.Load.sync" );
                pBlock->runLoadIfUnclaimed();
                return;
            }
            pBlock->runLoadIfUnclaimed();
            return;
        }
        TaskHandle handle = engine::getSubsystem<TaskManager>().emplaceTask( "AssetLoad", SW_DELEGATE_LAMBDA( TaskDelegate, [pBlock]()
        {
            pBlock->runLoadIfUnclaimed();
        } ) );
        handle.setPriority( TaskPriority::Low ); // 백그라운드 IO 큐에서 돌아 프레임 작업을 막지 않는다(AssetStreamingQueue 와 같다)
        handle.submit();
    }

    bool AssetLoadBlock::runCompletion( string& outError )
    {
        utf8 arrBuffer[constant::kMaxBuffer1024];
        formatstring( arrBuffer, constant::kMaxBuffer1024, "%# '%#' has no completion step", _pKindName, _path.c_str() );
        outError = arrBuffer;
        return false;
    }

    AssetCompletionStep AssetLoadBlock::beginCompletion( bool /*bBlocking*/, string& outError )
    {
        return runCompletion( outError ) ? AssetCompletionStep::Done : AssetCompletionStep::Failed;
    }

    AssetCompletionStep AssetLoadBlock::pollCompletion( bool /*bBlocking*/, string& /*outError*/ )
    {
        return AssetCompletionStep::Done;
    }

    void AssetLoadBlock::markReadyWithoutLoad()
    {
        SW_ASSERT( _phase.load( std::memory_order_acquire ) == kPhaseQueued );
        finish( true, string{} );
    }

    void AssetLoadBlock::markFailedWithoutLoad( string error )
    {
        SW_ASSERT( _phase.load( std::memory_order_acquire ) == kPhaseQueued );
        finish( false, std::move( error ) );
    }

    void AssetLoadBlock::executeLoad()
    {
        string     error;
        const bool bLoaded = runLoad( error );
        if ( bLoaded == false )
        {
            finish( false, std::move( error ) );
            return;
        }
        if ( hasCompletion() == false )
        {
            finish( true, string{} );
            return;
        }
        _phase.store( kPhaseAwaitingCompletion, std::memory_order_release );
        Futex::wakeAll( _phase );
        onAwaitingCompletion();
    }

    void AssetLoadBlock::executeCompletion( bool bBlocking )
    {
        string                    error;
        const AssetCompletionStep step = beginCompletion( bBlocking, error );
        settleCompletionStep( step, std::move( error ) );
    }

    void AssetLoadBlock::executeUploadPoll( bool bBlocking )
    {
        string                    error;
        const AssetCompletionStep step = pollCompletion( bBlocking, error );
        settleCompletionStep( step, std::move( error ) );
    }

    void AssetLoadBlock::settleCompletionStep( AssetCompletionStep step, string error )
    {
        switch ( step )
        {
            case AssetCompletionStep::Done:
            {
                finish( true, string{} );
                return;
            }
            case AssetCompletionStep::Failed:
            {
                finish( false, std::move( error ) );
                return;
            }
            case AssetCompletionStep::Submitted:
            {
                // 일은 나갔다. 끝남 확인은 펌프(`runUploadPollIfPending`)나 `wait()` 가 집는다.
                _phase.store( kPhaseUploading, std::memory_order_release );
                Futex::wakeAll( _phase );
                onUploadSubmitted();
                return;
            }
            case AssetCompletionStep::Busy:
            {
                // 지금은 낼 수 없다 — 아무도 집지 않은 완료 대기로 돌아가 다음 펌프가 다시 낸다.
                _phase.store( kPhaseAwaitingCompletion, std::memory_order_release );
                Futex::wakeAll( _phase );
                onAwaitingCompletion();
                return;
            }
        }
    }

    void AssetLoadBlock::finish( bool bSucceeded, string error )
    {
        // 실패는 이유와 함께 드러낸다 — 이유 없는 실패는 본문의 결함이다. 로그는 종류별 본문이 남긴다(메시는 경로마다 한 번만 경고한다).
        if ( bSucceeded == false )
        {
            SW_ASSERT( error.empty() == false );
            _error = std::move( error );
        }
        _state.store( bSucceeded ? AssetLoadState::Ready : AssetLoadState::Failed, std::memory_order_release );
        _phase.store( kPhaseDone, std::memory_order_release );
        Futex::wakeAll( _phase );
    }
} // namespace sw

namespace sw
{
    bool AssetReadySet::isComplete() const
    {
        for ( const shared_ptr<AssetLoadBlock>& pBlock : _listBlock )
        {
            if ( pBlock->getState() == AssetLoadState::Loading )
                return false;
        }
        return true;
    }

    bool AssetReadySet::isReady() const
    {
        for ( const shared_ptr<AssetLoadBlock>& pBlock : _listBlock )
        {
            if ( pBlock->getState() != AssetLoadState::Ready )
                return false;
        }
        return true;
    }

    bool AssetReadySet::hasFailed() const
    {
        for ( const shared_ptr<AssetLoadBlock>& pBlock : _listBlock )
        {
            if ( pBlock->getState() == AssetLoadState::Failed )
                return true;
        }
        return false;
    }

    size_t AssetReadySet::countLoading() const
    {
        size_t count = 0;
        for ( const shared_ptr<AssetLoadBlock>& pBlock : _listBlock )
        {
            if ( pBlock->getState() == AssetLoadState::Loading )
                ++count;
        }
        return count;
    }

    size_t AssetReadySet::countReady() const
    {
        size_t count = 0;
        for ( const shared_ptr<AssetLoadBlock>& pBlock : _listBlock )
        {
            if ( pBlock->getState() == AssetLoadState::Ready )
                ++count;
        }
        return count;
    }

    void AssetReadySet::appendFailures( vector<string>& outListError ) const
    {
        for ( const shared_ptr<AssetLoadBlock>& pBlock : _listBlock )
        {
            if ( pBlock->getState() != AssetLoadState::Failed )
                continue;
            utf8 arrBuffer[constant::kMaxBuffer1024];
            formatstring( arrBuffer, constant::kMaxBuffer1024, "%# %#: %#", pBlock->getKindName(), pBlock->getPath().c_str(), pBlock->getError().c_str() );
            outListError.emplace_back( arrBuffer );
        }
    }
} // namespace sw
