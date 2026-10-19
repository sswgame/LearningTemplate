/**
 * @file AudioBlock.h
 * @brief 블록 하나의 오디오를 담는 평면(planar) 형식입니다. 채널마다 `audio::kBlockFrameCount` 프레임이 연속으로 놓이고, 채널 수를 형식이 싣습니다.
 * @details 버스 입력, 보이스 작업 버퍼, 믹서 출력이 모두 이 형식이다. 채널별 램프 · 피크 · 이펙트 상태가 채널 수에 상관없이 같은 루프가 된다.
 *          인터리브는 장치 경계(`AudioEngine::render`)와 클립 읽기에만 남는다. 같은 블록에서 채널 `channelIndex` 의 시작은
 *          `channelIndex * audio::kBlockFrameCount` 이므로 블록 길이(`_frameCount`)가 짧아도 채널 간격은 변하지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    /**
     * @struct AudioBlockView
     * @brief 평면 블록을 가리키는 값입니다(소유하지 않습니다). 복사해서 넘기는 값 형식입니다.
     */
    struct AudioBlockView
    {
        float32* _pData{ nullptr };  ///< 채널 0 의 첫 샘플입니다. 없으면 빈 블록입니다.
        uint32   _channelCount{ 0 }; ///< 채널 수입니다.
        uint32   _frameCount{ 0 };   ///< 이번 블록의 유효 프레임입니다(`audio::kBlockFrameCount` 이하).

        /** @brief 블록이 비어 있는지입니다. */
        bool isEmpty() const { return _pData == nullptr; }
        /** @brief 채널 하나의 샘플(프레임 순서)입니다. */
        float32* getChannel( uint32 channelIndex ) const { return _pData + static_cast<size_t>( channelIndex ) * audio::kBlockFrameCount; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioBlockBuffer
     * @brief 평면 블록의 저장소입니다. 레이아웃의 채널 수만큼 한 번에 잡고, 블록마다 다시 잡지 않습니다.
     */
    class AudioBlockBuffer
    {
    public:
        /** @brief 레이아웃의 채널 수만큼 0 으로 채워 잡습니다. 초기화 때만 부릅니다(렌더 중 할당 없음). */
        void allocate( AudioChannelLayout layout )
        {
            _channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
            _listSample.assign( static_cast<size_t>( _channelCount ) * audio::kBlockFrameCount, 0.0f );
        }

        /** @brief 앞 @p frameCount 프레임을 모든 채널에서 0 으로 비웁니다. */
        void clear( uint32 frameCount )
        {
            const AudioBlockView view = makeView( frameCount );
            for ( uint32 channelIndex = 0; channelIndex < _channelCount; ++channelIndex )
            {
                Memory::set( view.getChannel( channelIndex ), 0, static_cast<size_t>( frameCount ) * sizeof( float32 ) );
            }
        }

        /** @brief 앞 @p frameCount 프레임을 가리키는 값을 만듭니다. */
        AudioBlockView makeView( uint32 frameCount )
        {
            AudioBlockView view;
            view._pData        = _listSample.data();
            view._channelCount = _channelCount;
            view._frameCount   = frameCount;
            return view;
        }

        /** @brief 채널 수입니다. 잡기 전에는 0 입니다. */
        uint32 getChannelCount() const { return _channelCount; }

    private:
        vector<float32> _listSample{}; /**< 채널 연속 저장입니다. */
        uint32          _channelCount{ 0 };
    };
} // namespace sw
