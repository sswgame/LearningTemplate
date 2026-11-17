#include "pch.h"

#include "Engine/Asset/PreloadSet.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"

namespace sw
{
    PreloadSet::PreloadSet( vector<string> listAssetPath, StreamingPriority priority, uint32 maxInFlight )
        : _listAssetPath{ std::move( listAssetPath ) }
        , _listRequest{}
        , _listPendingPin{}
        , _state{ make_shared<State>() }
        , _nextIndex{ 0 }
        , _maxInFlight{ maxInFlight }
        , _priority{ priority }
    {
        SW_ASSERT( _maxInFlight >= 1 );
    }

    PreloadSet::~PreloadSet()
    {
        // 늦게 온 콜백이 공유 상태에 다시 붙들지 않게 막고, 붙든 것을 지금 놓는다.
        _state->_bCancelled = SW_TRUE;
        _state->_pPins.reset();
    }

    void PreloadSet::enablePinning( AssetManager& manager, uint64 budgetBytes )
    {
        SW_ASSERT( _nextIndex == 0 );
        _state->_pPinManager = &manager;
        _state->_pPins       = make_unique<AssetPinSet>( budgetBytes );
    }

    void PreloadSet::onAssetRead( State& state, string_view path, bool bSuccess )
    {
        state._inFlightCount = state._inFlightCount > 0 ? state._inFlightCount - 1 : 0;
        ++state._completedCount;
        if ( bSuccess == false )
            state._listFailed.emplace_back( path );
    }

    bool PreloadSet::tryRequestPin( const string& path )
    {
        State& state = *_state;
        // 고정하는 종류는 캐시의 로드가 곧 읽기다 — 큐로 바이트를 읽고 버린 뒤 캐시가 다시 읽지 않는다(언리얼 `FStreamableHandle` 이 로드한 객체를 핸들이 쥔다).
        AssetPin pin = state._pPinManager->pinAsset( path );
        if ( pin._token == nullptr )
            return false;
        if ( pin._pLoad == nullptr )
        {
            ++state._completedCount; // 읽기는 그 종류의 다른 단계가 한다(머티리얼 초기화) — 있는지만 보고 지금 붙든다
            finishPin( path, std::move( pin._token ), true );
            return true;
        }
        ++state._inFlightCount;
        _listPendingPin.push_back( PendingPin{ path, std::move( pin ) } );
        return true;
    }

    void PreloadSet::finishPin( const string& path, shared_ptr<const void> token, bool bLoaded )
    {
        State& state = *_state;
        // 크기는 읽기와 같은 순서로 찾는다(파일을 열기만 한다) — 없으면 예전 큐 읽기도 실패했던 경로다.
        const optional<uint64> byteCount = bLoaded ? AssetIO::findAssetSize( AssetID{ path } ) : nullopt;
        if ( byteCount.has_value() == false )
        {
            state._listFailed.push_back( path );
            return;
        }
        (void)state._pPins->add( path, std::move( token ), *byteCount ); // 예산보다 큰 것은 읽기만 한 것으로 남는다
    }

    void PreloadSet::pollPendingPins()
    {
        State& state = *_state;
        for ( size_t index = 0; index < _listPendingPin.size(); )
        {
            PendingPin&          pending   = _listPendingPin[index];
            const AssetLoadState loadState = pending._pin._pLoad->getState();
            if ( loadState == AssetLoadState::Loading )
            {
                ++index;
                continue;
            }
            --state._inFlightCount;
            ++state._completedCount;
            finishPin( pending._path, std::move( pending._pin._token ), loadState == AssetLoadState::Ready );
            VectorUtil::removeAtSwap( _listPendingPin, index );
        }
    }

    void PreloadSet::update( AssetStreamingQueue& queue )
    {
        State& state = *_state;
        if ( state._bCancelled == SW_TRUE )
            return;
        pollPendingPins();
        // 한 번에 낼 수도 상한이다 — 곧바로 끝나는 고정(머티리얼)이 자리를 차지하지 않아도 한 프레임에 파일 열기를 몰지 않는다.
        for ( uint32 startedCount = 0; startedCount < _maxInFlight && _nextIndex < _listAssetPath.size() && state._inFlightCount < _maxInFlight; ++startedCount )
        {
            const string& path = _listAssetPath[_nextIndex++];
            if ( state._pPins != nullptr && tryRequestPin( path ) )
                continue;
            shared_ptr<State> shared = _state;
            // 바이트까지 읽는다(있는지만 보는 요청은 디스크 · 팩을 데우지 않는다). 바이트는 버린다 — 고정하지 않는 종류라 객체는 그 에셋을 쓰는 쪽이 만든다.
            const auto onDone = [shared]( string_view donePath, bool bSuccess, const vector<uint8>& )
            {
                onAssetRead( *shared, donePath, bSuccess );
            };
            ++state._inFlightCount;
            StreamingRequest request   = queue.requestAssetData( path, _priority, SW_DELEGATE_LAMBDA( OnStreamingDataCompleteDelegate, onDone ) );
            const bool       bAccepted = request.isValid();
            _listRequest.push_back( std::move( request ) );
            if ( bAccepted == false )
            {
                // 큐가 받지 않았다(빈 경로) — 실패로 센다. 콜백은 오지 않는다.
                --state._inFlightCount;
                ++state._completedCount;
                state._listFailed.push_back( path );
                SW_LOG_WARNING_ID( "PreloadSet.PreloadNotAcceptedStreaming", "Preload of '%#' was not accepted by the streaming queue", path );
            }
        }
    }

    void PreloadSet::cancel( AssetStreamingQueue& queue )
    {
        State& state      = *_state;
        state._bCancelled = SW_TRUE;
        if ( state._pPins != nullptr )
            state._pPins->clear();
        // 로드를 기다리던 고정은 큐의 취소처럼 실패로 센다. 로드 자체는 캐시가 끝까지 한다(다른 쪽이 같은 로드를 받을 수 있다).
        for ( const PendingPin& pending : _listPendingPin )
        {
            --state._inFlightCount;
            ++state._completedCount;
            state._listFailed.push_back( pending._path );
        }
        _listPendingPin.clear();
        // 이 세트가 낸 요청만 거둔다 — 같은 경로를 기다리는 다른 쪽(다른 세트 · 로더)의 요청은 그대로 받는다. 끝난 요청의 취소는 아무것도 하지 않는다.
        for ( const StreamingRequest& request : _listRequest )
        {
            queue.cancelRequest( request );
        }
    }

    void PreloadSet::touch( string_view path )
    {
        if ( _state->_pPins != nullptr )
            _state->_pPins->touch( path );
    }

    float32 PreloadSet::getProgress() const
    {
        if ( _listAssetPath.empty() )
            return 1.0f;
        return static_cast<float32>( _state->_completedCount ) / static_cast<float32>( _listAssetPath.size() );
    }

    bool PreloadSet::isComplete() const
    {
        return _state->_completedCount >= _listAssetPath.size();
    }

    const vector<string>& PreloadSet::getFailedAssets() const
    {
        return _state->_listFailed;
    }
} // namespace sw
