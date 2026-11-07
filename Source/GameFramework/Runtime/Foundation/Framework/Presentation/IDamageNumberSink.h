/**
 * @file GameFramework/Runtime/Foundation/Framework/Presentation/IDamageNumberSink.h
 * @brief 맞은 자리에 떠오르는 피해 숫자를 내는 창구입니다 — 어빌리티 · 유닛 능력치 같은 게임 코드가 월드 위젯(UI)을 모르고 숫자를 냅니다.
 * @details 구현은 클라이언트 전용 소스(`GameFramework/Client/Marker/DamageNumberComponent`)의 모듈 서브시스템이고, 전용 서버에는 없어 `game::findSubsystem` 이 nullptr 이다.
 *          언리얼의 게임플레이 큐(피해 표시는 클라이언트의 큐 처리기가 그린다)와 같은 갈림이다(기억).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class GameObjectManager;

    /** @class IDamageNumberSink @brief 피해 숫자를 받는 쪽입니다. 게임 스레드 · 틱 워커 어디서 불러도 된다(틱 중이면 구현이 틱 뒤로 미룬다). */
    class IDamageNumberSink
    {
    public:
        virtual ~IDamageNumberSink() = default;

        /** @brief @p manager 의 월드 @p position 에 @p value 를 띄웁니다. */
        virtual void spawnDamageNumber( GameObjectManager& manager, const float3& position, int32 value ) = 0;
    };
} // namespace sw
