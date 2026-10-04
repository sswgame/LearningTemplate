/**
 * @file AudioMixer.h
 * @brief 믹서 그래프의 실행 상태 — 버스마다 입력 버퍼 · 페이더 · 음소거/솔로 · 센드를 들고 블록 하나를 처리합니다.
 * @details `AudioMixerDesc`(데이터)로 만들고, 오디오 스레드만 만집니다. 블록 순서: `beginBlock` → 보이스가 버스 입력에 더함 → `process` 가
 *          처리 순서(보내는 쪽 먼저)대로 버스마다 페이더를 램프해 곱하고, 센드 대상 · 부모에 더하고, master 의 출력을 돌려줍니다.
 *          페이더 게인 = 데이터 볼륨(dB) × 사용자 볼륨(설정 메뉴) × 스냅샷 오프셋(dB) × 음소거/솔로입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioMixerDesc.h"

namespace sw
{
    /**
     * @class AudioMixer
     * @brief 버스 그래프 하나입니다.
     */
    class SW_API AudioMixer
    {
    public:
        AudioMixer();
        ~AudioMixer();

        AudioMixer( const AudioMixer& )            = delete;
        AudioMixer& operator=( const AudioMixer& ) = delete;

        /** @brief 데이터로 버스를 만듭니다. 데이터가 검사를 통과하지 못하면(이름 · 고리) false 이고 그래프는 비어 있습니다. */
        [[nodiscard]] bool initialize( const AudioMixerDesc& desc );

        /** @brief 블록을 시작합니다 — 모든 버스 입력을 0 으로 비웁니다. */
        void beginBlock( uint32 frameCount );
        /**
         * @brief 처리 순서대로 버스를 처리하고 master 출력을 @p pOutput(스테레오 교차, @p frameCount 프레임)에 씁니다.
         * @details 입력 버퍼는 처리 뒤에도 남지만 다음 `beginBlock` 이 비웁니다.
         */
        void process( float32* pOutput, uint32 frameCount );

        /** @brief 버스 수입니다. */
        uint32 getBusCount() const { return static_cast<uint32>( _listBusState.size() ); }
        /** @brief 이름의 버스 번호입니다. 없으면 -1 입니다. */
        int32 findBusIndex( const hashed_string& name ) const;
        /** @brief 버스 이름입니다. */
        const hashed_string& getBusName( uint32 busIndex ) const { return _listBusState[busIndex]._name; }
        /** @brief 버스의 입력 버퍼(스테레오 교차, 블록 길이)입니다. 보이스가 여기에 더합니다. */
        float32* getBusInput( uint32 busIndex ) { return _listBusState[busIndex]._listInput.data(); }
        /** @brief 데이터가 정한 보이스 상한 · 들림 문턱입니다. */
        const AudioMixerDesc& getDesc() const { return _desc; }

        /** @brief 사용자 볼륨(선형, [0, 1])을 정합니다 — 설정 메뉴의 버스 볼륨입니다. */
        void setBusUserVolume( uint32 busIndex, float32 volume );
        /** @brief 사용자 볼륨입니다. */
        float32 getBusUserVolume( uint32 busIndex ) const { return _listBusState[busIndex]._userVolume; }
        /** @brief 음소거를 정합니다. */
        void setBusMuted( uint32 busIndex, bool bMuted );
        /** @brief 음소거 중인지입니다. */
        bool isBusMuted( uint32 busIndex ) const { return _listBusState[busIndex]._bMuted == SW_TRUE; }
        /** @brief 솔로를 정합니다. 하나라도 솔로면, 솔로 버스의 조상 · 자손이 아닌 버스는 소리를 내지 않습니다. */
        void setBusSolo( uint32 busIndex, bool bSolo );
        /** @brief 솔로인지입니다. */
        bool isBusSolo( uint32 busIndex ) const { return _listBusState[busIndex]._bSolo == SW_TRUE; }
        /** @brief 데이터 볼륨(dB) 위에 더하는 오프셋(dB)입니다 — 스냅샷이 씁니다. */
        void setBusVolumeOffsetDb( uint32 busIndex, float32 offsetDb ) { _listBusState[busIndex]._volumeOffsetDb = offsetDb; }
        /** @brief 센드 레벨(dB)을 바꿉니다. 그 센드가 없으면 false 입니다. */
        bool setSendLevelDb( uint32 busIndex, uint32 targetBusIndex, float32 levelDb );
        /** @brief 센드 레벨(dB)입니다. 그 센드가 없으면 데이터 바닥 값입니다. */
        float32 getSendLevelDb( uint32 busIndex, uint32 targetBusIndex ) const;

        /** @brief 이 블록에서 정한 버스 페이더 게인(선형)입니다 — 데이터 × 사용자 × 스냅샷 × 음소거/솔로. */
        float32 computeBusGain( uint32 busIndex ) const;
        /** @brief 마지막 블록의 버스 출력 피크(절댓값 최대)입니다. */
        float32 getBusPeak( uint32 busIndex ) const { return _listBusState[busIndex]._peak; }

    private:
        /** @brief 솔로가 걸린 버스가 있으면 각 버스가 솔로 경로 위인지 다시 셉니다. */
        void refreshSoloPaths();

    private:
        /** @brief 센드 하나의 실행 상태입니다. */
        struct Send
        {
            uint32  _targetIndex{ 0 };
            float32 _levelDb{ 0.0f };
            float32 _gain{ 1.0f }; ///< 지금 램프 중인 선형 레벨
            bool    _bPreFader{ false };
        };

        /** @brief 버스 하나의 실행 상태입니다. */
        struct Bus
        {
            vector<float32>        _listInput{};
            vector<Send>           _listSend{};
            hashed_string          _name{};
            int32                  _parentIndex{ -1 };
            float32                _volumeDb{ 0.0f };
            float32                _volumeOffsetDb{ 0.0f };
            float32                _userVolume{ 1.0f };
            float32                _gain{ 0.0f }; ///< 지난 블록 끝의 페이더 게인(램프 시작점)
            float32                _peak{ 0.0f };
            uint8                  _bMuted      : 1;
            uint8                  _bSolo       : 1;
            uint8                  _bAudible    : 1; ///< 솔로 규칙으로 소리를 내도 되는지
            uint8                  _bFirstBlock : 1; ///< 처음 처리 — 게인을 램프하지 않고 바로 건다
            [[maybe_unused]] uint8 _reservedBus : 4;

            Bus()
                : _bMuted{ SW_FALSE }
                , _bSolo{ SW_FALSE }
                , _bAudible{ SW_TRUE }
                , _bFirstBlock{ SW_TRUE }
                , _reservedBus{ 0 }
            {
            }
        };

        AudioMixerDesc _desc;            /**< 만든 데이터입니다(보이스 상한 · 문턱을 엔진이 읽는다). */
        vector<Bus>    _listBusState;    /**< 버스(데이터 순서)입니다. */
        vector<uint32> _listOrder;       /**< 처리 순서입니다. */
        uint32         _masterIndex;     /**< master 버스 번호입니다. */
        uint32         _blockFrameCount; /**< 이번 블록 길이입니다. */
    };
} // namespace sw
