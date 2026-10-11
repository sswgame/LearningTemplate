/**
 * @file MechArenaSnapshot.h
 * @brief 기체 대전의 서버 권위 상태를 바이트로 만듭니다. 판 단계, 남은 시간, 팀 게이지, 조종사(상태, 위치, 체력, 부스트, 다운치, 형태, 탄창, 록온, 콤보, 스킬), 탄을 담습니다.
 * @details 장르 키트는 넷 키트를 include 하지 않는다. 게임이 이 바이트를 `GF_NetClientServer` 의 스냅샷에 실어 보낸다. 쓰기는 Core 의 `BitWriter` 를 쓴다
 *          (위치는 0.01m, 체력은 0.1, 비율은 1/1023 단위로 양자화한다). 읽기는 깨진 바이트를 받으면 false 를 반환한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 조종사 하나의 보이는 상태입니다. */
    struct MechPilotSnapshot
    {
        float3         _position{};
        float32        _health{ 0.0f };
        float32        _boostHeat{ 0.0f }; ///< 부스트 열 비율 0..1(1이면 오버히트)
        float32        _downRatio{ 0.0f }; ///< 쌓인 다운치 비율 0..1
        int32          _team{ 0 };
        int32          _mechIndex{ -1 }; ///< 카탈로그 인덱스
        int32          _mode{ 0 };
        int32          _magazineAmmo{ 0 }; ///< 지금 형태 첫 주무기의 탄창
        int32          _lockTarget{ -1 };
        int32          _comboStage{ -1 };
        uint32         _activeSkillMask{ 0 }; ///< 켜진 스킬 슬롯 비트
        MechPilotState _state{ MechPilotState::Waiting };
        uint8          _bOverheated{ SW_FALSE };
        uint8          _bInvulnerable{ SW_FALSE };
        uint8          _bReloading{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 탄 하나의 보이는 상태입니다. */
    struct MechProjectileSnapshot
    {
        float3 _position{};
        int32  _owner{ -1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 걸음의 판 전체 상태입니다. */
    struct MechArenaSnapshot
    {
        vector<int32>                  _listTeamGauge{};
        vector<MechPilotSnapshot>      _listPilot{};
        vector<MechProjectileSnapshot> _listProjectile{};
        float32                        _remainingTime{ 0.0f };
        float32                        _arenaHalfSize{ 200.0f }; ///< 위치 양자화 범위(쓰는 쪽의 설정이며 바이트에 함께 실린다)
        float32                        _ceiling{ 60.0f };
        uint32                         _tick{ 0 };
        int32                          _winningTeam{ -1 };
        MatchPhase                     _phase{ MatchPhase::Waiting };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MechArenaSnapshotCodec
     * @brief 스냅샷과 비트를 서로 변환합니다. 같은 스냅샷은 항상 같은 바이트가 됩니다(결정적).
     */
    struct SW_GF_API MechArenaSnapshotCodec
    {
        static void write( const MechArenaSnapshot& snapshot, BitWriter& outWriter );
        /** @brief 스냅샷을 읽습니다. 바이트가 모자라거나 값이 범위를 벗어나면 false 를 반환하며, 이때 @p outSnapshot 은 믿을 수 없습니다. */
        [[nodiscard]] static bool read( BitReader& reader, MechArenaSnapshot& outSnapshot );
    };
} // namespace sw
