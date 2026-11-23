/**
 * @file ArenaUnitView.h
 * @brief 아레나 유닛의 종류와, 디렉터가 프레임마다 적는 유닛 스냅샷 한 줄입니다. 규칙(탄의 맞음)과 컴포넌트가 함께 읽습니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 아레나 유닛의 종류입니다 — 어빌리티 세트 · 모양 · 조종자(AI 프리팹)를 고릅니다. */
    ENUM()
    enum class ArenaUnitKind : uint8
    {
        Player = 0, ///< 플레이어 조종자(또는 자동 전투 AI)가 쥔다 — 세트 "ArenaPlayer"
        Grunt,      ///< 다가와 때린다, 맞으면 가시로 되갚는다 — 세트 "ArenaGrunt"
        Caster      ///< 거리를 두고 화염탄을 쏜다 — 세트 "ArenaCaster"
    };
} // namespace sw

namespace sw
{
    /** @brief 이번 프레임의 유닛 하나입니다. 디렉터가 `PrePhysics` 에서 기록하고 다른 그룹이 읽습니다. */
    struct ArenaUnitView
    {
        GameObjectHandle _object{};
        float3           _position{};
        int32            _unitID{ constant::invalid_index::kInt32 }; ///< 판 안에서 겹치지 않는 유닛 번호(`ArenaUnit::_unitID`) — 탄의 맞은 대상 번호다. 다른 유닛이 빠져도 밀리지 않는다
        ArenaUnitKind    _kind{ ArenaUnitKind::Grunt };
        uint8            _bAlive{ SW_FALSE };      ///< 쓰러지지 않았다(Dead 태그 없음).
        uint8            _bPlayerTeam{ SW_FALSE }; ///< `Team.Player` 태그.
        uint8            _bEnemyTeam{ SW_FALSE };  ///< `Team.Enemy` 태그.
    };
} // namespace sw
