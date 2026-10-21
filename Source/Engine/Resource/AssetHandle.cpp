#include "pch.h"

#include "Engine/Resource/AssetHandle.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"

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
                executeCompletion();
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
        executeCompletion();
        return true;
    }

    void AssetLoadBlock::startLoad( const shared_ptr<AssetLoadBlock>& pBlock, AssetLoadStart start )
    {
        SW_ASSERT( pBlock != nullptr );
        if ( pBlock->_phase.load( std::memory_order_acquire ) != kPhaseQueued )
            return; // 캐시 적중 · 틀린 요청으로 이미 끝난 블록이다
        if ( start == AssetLoadStart::OnCallingThread || engine::areEngineServicesBound() == false )
        {
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

    void AssetLoadBlock::executeCompletion()
    {
        string     error;
        const bool bCompleted = runCompletion( error );
        finish( bCompleted, std::move( error ) );
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
