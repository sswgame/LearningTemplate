/**
 * @file MechArenaSnapshot.h
 * @brief 기체 대전의 서버 권위 상태입니다. 판 단계, 남은 시간, 팀 게이지, 조종사(상태, 위치, 체력, 부스트, 다운치, 형태, 탄창, 록온, 콤보, 스킬), 탄을 담습니다.
 * @details 바이트는 고정 배치형(B, `FixedLayout`)입니다. 코드 생성기가 아래 `PROPERTY` 의 범위 키로 코덱을 만들고, 손 코덱과 손 버전 상수는 없습니다.
 *          범위 밖 정수, 255 개를 넘는 목록, `Step` 없는 실수의 범위 밖 값(1 보다 작은 판 크기)은 잘라 쓰지 않고 쓰기가 실패합니다.
 *          `Step` 이 붙은 실수(위치 0.01 m, 체력 0.1, 비율 0.001, 시간 0.01 초)는 손실 인코딩이므로 범위 밖 값을 경계로 잘라 씁니다.
 *          **필드 순서가 와이어 형식입니다.** 위치 범위가 `_arenaHalfSize` · `_ceiling` 이므로 그 둘이 목록보다 앞에 있습니다.
 *          장르 키트는 넷 키트를 include 하지 않습니다. 게임이 `MechArenaWorld::writeState` 의 바이트를 채널에 싣고, 받는 쪽은 `FixedLayout::read` 로 읽습니다.
 *          바이트 머리에 스키마 해시가 있으므로 배치가 다른 빌드의 바이트는 `SchemaMismatch` 로 거절됩니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

namespace sw
{
    /** @brief 조종사 하나의 보이는 상태입니다. 위치 범위는 바깥 `MechArenaSnapshot` 의 판 크기입니다. */
    REFLECT()
    struct MechPilotSnapshot
    {
        REFLECT_BODY();
        PROPERTY( Step = 0.01, Min = "-_arenaHalfSize, 0, -_arenaHalfSize", Max = "_arenaHalfSize, _ceiling, _arenaHalfSize" )
        float3 _position{};
        PROPERTY( Step = 0.1, Min = 0, Max = 100000 )
        float32 _health{ 0.0f };
        PROPERTY( Step = 0.001, Min = 0, Max = 1 )
        float32 _boostHeat{ 0.0f }; ///< 부스트 열 비율 0..1(1이면 오버히트)
        PROPERTY( Step = 0.001, Min = 0, Max = 1 )
        float32 _downRatio{ 0.0f }; ///< 쌓인 다운치 비율 0..1
        PROPERTY( Min = 0, Max = 255 )
        int32 _team{ 0 };
        PROPERTY( Min = -1, Max = 255 )
        int32 _mechIndex{ -1 }; ///< 카탈로그 인덱스
        PROPERTY( Min = 0, Max = 255 )
        int32 _mode{ 0 };
        PROPERTY( Min = 0, Max = 4095 )
        int32 _magazineAmmo{ 0 }; ///< 지금 형태 첫 주무기의 탄창
        PROPERTY( Min = -1, Max = 255 )
        int32 _lockTarget{ -1 };
        PROPERTY( Min = -1, Max = 255 )
        int32 _comboStage{ -1 };
        PROPERTY()
        uint32 _activeSkillMask{ 0 }; ///< 켜진 스킬 슬롯 비트
        PROPERTY()
        MechPilotState _state{ MechPilotState::Waiting };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bOverheated{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bInvulnerable{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bReloading{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 탄 하나의 보이는 상태입니다. 위치 범위는 바깥 `MechArenaSnapshot` 의 판 크기입니다. */
    REFLECT()
    struct MechProjectileSnapshot
    {
        REFLECT_BODY();
        PROPERTY( Step = 0.01, Min = "-_arenaHalfSize, 0, -_arenaHalfSize", Max = "_arenaHalfSize, _ceiling, _arenaHalfSize" )
        float3 _position{};
        PROPERTY( Min = -1, Max = 255 )
        int32 _owner{ -1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 걸음의 판 전체 상태입니다. 같은 스냅샷은 항상 같은 바이트가 됩니다(결정적). */
    REFLECT()
    struct MechArenaSnapshot
    {
        REFLECT_BODY();
        PROPERTY( Min = 1, Max = 100000 )
        float32 _arenaHalfSize{ 200.0f }; ///< 위치 양자화 범위입니다. 쓰는 쪽의 설정이며 바이트에 함께 실립니다
        PROPERTY( Min = 1, Max = 100000 )
        float32 _ceiling{ 60.0f };
        PROPERTY( Step = 0.01, Min = 0, Max = 100000 )
        float32 _remainingTime{ 0.0f };
        PROPERTY()
        uint32 _tick{ 0 };
        PROPERTY( MaxCount = 255 )
        vector<int32> _listTeamGauge{};
        PROPERTY( MaxCount = 255 )
        vector<MechPilotSnapshot> _listPilot{};
        PROPERTY( MaxCount = 255 )
        vector<MechProjectileSnapshot> _listProjectile{};
        PROPERTY( Min = -1, Max = 255 )
        int32 _winningTeam{ -1 };
        PROPERTY()
        MatchPhase _phase{ MatchPhase::Waiting };
    };
} // namespace sw
