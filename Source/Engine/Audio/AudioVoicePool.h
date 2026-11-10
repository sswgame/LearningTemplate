/**
 * @file AudioVoicePool.h
 * @brief 오디오 엔진의 보이스 슬롯 표입니다. 슬롯을 잡고 · 돌려주고, 블록마다 실제로 섞을 보이스를 고릅니다(나머지는 가상 · 정지).
 * @details `AudioEngine` 에서 뗐다 — 슬롯의 수명(잡을 때 비우기 · 돌려줄 때 지우기)과 가상화 정책(우선순위 → 들림 → 먼저 낸 것)은 슬롯만 본다.
 *          버스 · 감쇠 · 인스턴스 번호를 풀고 목표를 정하는 일은 엔진이 한다(언리얼 FMixerSourceManager 의 소스 풀 · 가상화와 같은 자리).
 *          오디오 스레드만 만진다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/AudioVoice.h"

namespace sw
{
    /** @brief 보이스 슬롯 하나입니다. */
    struct AudioVoiceSlot
    {
        AudioVoice       _voice{};
        hashed_string    _clipPath{};
        hashed_string    _busName{};
        hashed_string    _attenuationName{};
        AudioPlayingID   _playingID{ 0 };
        AudioEmitterID   _emitterID{ 0 };
        float32          _volume{ 1.0f }; ///< 기본 볼륨(선형)
        float32          _pan{ 0.0f };
        float32          _pitchRatio{ 1.0f };
        float32          _fadeInSeconds{ 0.0f };
        float32          _audibility{ 0.0f }; ///< 이번 블록 목표 게인(버스 페이더는 넣지 않는다)
        float32          _distance{ 0.0f };
        uint32           _busIndex{ 0 };
        optional<uint32> _attenuationIndex{}; ///< 감쇠 테이블 번호다(비어 있으면 2D).
        optional<uint32> _instanceIndex{};    ///< 이벤트 인스턴스(비어 있으면 클립 재생)
        int32            _priority{ 50 };
        int32            _musicSegment{ constant::invalid_index::kInt32 }; ///< 음악 레이어의 구간이다(-1 = 음악 아님).
        int32            _musicLayer{ constant::invalid_index::kInt32 };   ///< 음악 레이어 번호다.
        float32          _musicGain{ 1.0f };                               ///< 파라미터로 페이드하는 레이어 게인이다.
        uint64           _startFrame{ 0 };                                 ///< 음악에서 소리가 시작할 렌더 프레임이다. 클립이 늦게 와도 박을 지킨다.
        AudioVirtualMode _virtualMode{ AudioVirtualMode::Virtualize };
        bool             _bLoop{ SW_FALSE };        ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool             _bInUse{ false };          ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool             _bWaitingForClip{ false }; ///< 클립 디코드를 기다린다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool             _bPausedRequest{ false };  ///< 클립을 기다리는 동안 들어온 일시정지 요청이다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool             _bVirtual{ false };        ///< 이번 블록에 섞지 않는다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @class AudioVoicePool
     * @brief 보이스 슬롯을 고정 개수로 듭니다. 개수는 믹서 그래프의 보이스 상한(`_maxVoiceCount`)입니다.
     */
    class SW_API AudioVoicePool
    {
    public:
        /** @brief 슬롯을 모두 버리고 @p count 개의 빈 슬롯으로 둡니다. */
        void reset( uint32 count );
        /** @brief 슬롯을 @p count 개까지 늘립니다(줄이지 않는다 — 살아 있는 보이스가 있다). */
        void growTo( uint32 count );
        void clear() { _listSlot.clear(); }

        /** @brief 빈 슬롯을 비운 채로 잡습니다. 없으면 nullptr 입니다. */
        AudioVoiceSlot* allocate();
        /** @brief 슬롯을 돌려줍니다. */
        static void free( AudioVoiceSlot& slot );

        /**
         * @brief 이번 블록에 섞을 보이스를 고릅니다. 들리지 않는(@p inaudible 미만) 보이스는 가상으로 두거나(원샷이면) 돌려주고,
         *        나머지 가운데 @p maxRealCount(늘 섞는 `KeepReal` 을 뺀 몫)를 넘는 것은 가상으로 둡니다 — 들리는 원샷은 한 블록 줄여 끊습니다.
         */
        void selectRealVoices( float32 inaudible, uint32 maxRealCount );

        uint32                getCount() const { return static_cast<uint32>( _listSlot.size() ); }
        AudioVoiceSlot*       begin() { return _listSlot.data(); }
        AudioVoiceSlot*       end() { return _listSlot.data() + _listSlot.size(); }
        const AudioVoiceSlot* begin() const { return _listSlot.data(); }
        const AudioVoiceSlot* end() const { return _listSlot.data() + _listSlot.size(); }

    private:
        vector<AudioVoiceSlot> _listSlot;
        vector<uint32>         _listOrder; ///< 고르기의 작업 목록(블록마다 다시 쓴다)
    };
} // namespace sw
