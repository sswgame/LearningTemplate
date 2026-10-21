#include "pch.h"

#include "Engine/Resource/PreloadSet.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "PreloadSet" );

    PreloadSet::PreloadSet( vector<string> listAssetPath, StreamingPriority priority, uint32 maxInFlight )
        : _listAssetPath{ std::move( listAssetPath ) }
        , _state{ make_shared<State>() }
        , _nextIndex{ 0 }
        , _maxInFlight{ maxInFlight }
        , _priority{ priority }
    {
        SW_ASSERT( _maxInFlight >= 1 );
    }

    PreloadSet::~PreloadSet() = default;

    void PreloadSet::update( AssetStreamingQueue& queue )
    {
        State& state = *_state;
        if ( state._bCancelled == SW_TRUE )
            return;
        while ( _nextIndex < _listAssetPath.size() && state._inFlightCount < _maxInFlight )
        {
            const string&     path   = _listAssetPath[_nextIndex++];
            shared_ptr<State> shared = _state;
            // 바이트까지 읽는다(있는지만 보는 요청은 디스크 · 팩을 데우지 않는다). 바이트는 버린다 — 객체는 그 에셋을 쓰는 쪽이 만든다.
            const auto onDone = [shared]( string_view donePath, bool bSuccess, const vector<uint8>& bytes )
            {
                (void)bytes;
                shared->_inFlightCount = shared->_inFlightCount > 0 ? shared->_inFlightCount - 1 : 0;
                ++shared->_completedCount;
                if ( bSuccess == false )
                    shared->_listFailed.emplace_back( donePath );
            };
            ++state._inFlightCount;
            if ( queue.requestAssetData( path, _priority, SW_DELEGATE_LAMBDA( OnStreamingDataCompleteDelegate, onDone ) ) == false )
            {
                // 큐가 받지 않았다(빈 경로) — 실패로 센다. 콜백은 오지 않는다.
                --state._inFlightCount;
                ++state._completedCount;
                state._listFailed.push_back( path );
                SW_LOG_WARNING( "Preload of '%#' was not accepted by the streaming queue", path );
            }
        }
    }

    void PreloadSet::cancel( AssetStreamingQueue& queue )
    {
        State& state      = *_state;
        state._bCancelled = SW_TRUE;
        // 걸어 둔 요청(이미 넘긴 경로 중 끝나지 않은 것)을 큐에서 거둔다 — 경로를 모두 돌며 끝나지 않은 것만 취소한다.
        for ( size_t index = 0; index < _nextIndex; ++index )
        {
            if ( queue.isStreaming( _listAssetPath[index] ) )
                queue.cancelRequest( _listAssetPath[index] );
        }
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
