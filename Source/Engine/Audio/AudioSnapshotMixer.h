/**
 * @file AudioSnapshotMixer.h
 * @brief 믹스 스냅샷(버스 볼륨 오프셋 · 센드 · 이펙트 파라미터의 세기 블렌드)과 버스 사용자 상태(볼륨 · 음소거 · 솔로)를 그래프에 거는 믹서 상태를 선언합니다.
 * @details `AudioEngine` 이 하나를 들고 오디오 스레드에서만 만진다. 그래프(`AudioMixer`)를 바꿔도 이름으로 남는 상태라 그래프 밖에 둔다.
 *          FMOD Studio 의 스냅샷 · 버스 페이더, 언리얼 Sound Mix(`USoundMix` 의 페이드 인 · 아웃과 클래스 조정)와 같은 자리다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct AudioSnapshotDesc;

    class AudioMixer;
} // namespace sw

namespace sw
{
    /**
     * @class AudioSnapshotMixer
     * @brief 켜 둔 스냅샷의 세기와 버스 이름별 사용자 상태를 들고, 블록마다 그래프에 적용합니다.
     */
    class SW_API AudioSnapshotMixer
    {
    public:
        /** @brief 켠 스냅샷을 모두 지웁니다(엔진 종료). 버스 사용자 상태는 설정 메뉴의 값이라 남깁니다. */
        void clearSnapshots() { _listSnapshot.clear(); }

        /** @brief 스냅샷을 켭니다. `_fadeInSeconds` 동안 세기를 1 로 올리고, 가장 나중에 켠 것이 마지막에 적용되도록 목록 끝으로 옮깁니다. 그래프에 없는 이름이면 오류를 남깁니다. */
        void startSnapshot( const AudioMixer& mixer, const hashed_string& name );
        /** @brief 스냅샷을 끕니다. `_fadeOutSeconds` 동안 세기를 0 으로 내립니다. */
        void stopSnapshot( const hashed_string& name );
        /** @brief 스냅샷 세기를 직접 정합니다(페이드 없음, 게임이 세기를 끌고 간다). 그래프에 없는 이름이면 오류를 남깁니다. */
        void setSnapshotIntensity( const AudioMixer& mixer, const hashed_string& name, float32 intensity );
        /** @brief 스냅샷의 현재 세기입니다. 켜져 있지 않으면 0 입니다. */
        float32 getSnapshotIntensity( const hashed_string& name ) const;

        /** @brief 버스의 사용자 볼륨(선형, [0, 1])을 정하고 그래프에 적용합니다. */
        void setBusUserVolume( AudioMixer& mixer, const hashed_string& bus, float32 volume );
        void setBusMuted( AudioMixer& mixer, const hashed_string& bus, bool bMuted );
        void setBusSolo( AudioMixer& mixer, const hashed_string& bus, bool bSolo );

        /** @brief 새 그래프로 바뀐 뒤 부릅니다. 버스 사용자 상태를 다시 적용하고, 스냅샷 설명자를 이름으로 다시 찾습니다(없어진 이름은 지운다). */
        void adoptMixer( AudioMixer& mixer );
        /** @brief 블록 하나만큼 스냅샷 세기를 옮기고, 버스 오프셋 · 센드 · 이펙트 파라미터에 적용합니다. */
        void update( AudioMixer& mixer );

    private:
        /** @brief 스냅샷 하나의 실행 상태입니다. 켠 순서대로 목록에 둡니다. */
        struct SnapshotState
        {
            hashed_string            _name{};
            const AudioSnapshotDesc* _pDesc{ nullptr };
            float32                  _intensity{ 0.0f };
            float32                  _target{ 0.0f };
            bool                     _bDriven{ false }; ///< 게임이 세기를 직접 정한다.
        };

        /** @brief 버스 이름에 남는 사용자 상태입니다. 그래프를 바꾸면 다시 적용합니다. */
        struct BusUserState
        {
            hashed_string _bus{};
            float32       _volume{ 1.0f };
            bool          _bMuted{ false };
            bool          _bSolo{ false };
        };

        /** @brief 이름에 해당하는 스냅샷 슬롯을 반환합니다. 없으면 만들고, 그래프에 없는 이름이면 nullptr 를 반환합니다. */
        SnapshotState* getOrCreateSnapshot( const AudioMixer& mixer, const hashed_string& name );
        /** @brief 이름에 해당하는 사용자 상태 슬롯을 반환합니다. 없으면 만듭니다. */
        BusUserState& getOrCreateBusUserState( const hashed_string& bus );
        /** @brief 사용자 상태(볼륨, 음소거, 솔로)를 그래프에 적용합니다. 그래프에 없는 버스면 그대로 둡니다. */
        static void applyBusUserState( AudioMixer& mixer, const BusUserState& state );

    private:
        vector<SnapshotState> _listSnapshot;     /**< 켜 둔 스냅샷입니다. 켠 순서대로 둡니다. */
        vector<BusUserState>  _listBusUserState; /**< 버스 이름의 사용자 상태입니다. */
    };
} // namespace sw
