/**
 * @file MountUtil.h
 * @brief 타기 · 내리기 — 탑승 = 조종자가 빙의를 탈것으로 옮기는 것. 탑승자 폰은 좌석 소켓에 붙고 이동이 멈춥니다. 플레이어와 NPC 가 같은 함수로 탑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 타기의 결과입니다. */
    ENUM()
    enum class MountResult : uint8
    {
        Mounted = 0,  ///< 탔다
        SeatTaken,    ///< 누가 앉아 있다 — 또는 탑승자가 이미 어딘가에 앉아 있다
        TooFar,       ///< 좌석의 타는 거리 밖이다
        NoSocket,     ///< 탈것 외형에 좌석 소켓이 없다 — 또는 붙일 수 없다
        NotPossessed, ///< 운전석인데 탑승자를 쥔 조종자가 없다(넘겨줄 빙의가 없다)
        NoVehiclePawn ///< 운전석인데 탈것에 폰이 없다
    };
} // namespace sw

namespace sw
{
    class PawnComponent;
    class VehicleSeatComponent;

    /**
     * @struct MountUtil
     * @brief 타기 · 내리기입니다. 게임 스레드, 틱 밖에서 부릅니다(틱 안이면 `GameObjectManager::executeOrDeferPostTick` 으로 미룬다 — `MountInteractionComponent` 가 그렇게 한다).
     * @details 언리얼에는 기본 틀이 없고 관례가 "탈것 폰 + `Possess` + 탑승자 `AttachToComponent( Seat socket )` + 탑승자 이동 끔" 입니다. 그 관례를 기반 코드로 둡니다.
     *          타기 순서: 좌석 · 거리 확인 → 소켓 변환(탈것 외형의 소켓, 없으면 좌석 오프셋) → 탑승자 `SocketBindingComponent::bindToSocket` →
     *          폰 이동 멈춤 · 캐릭터 컨트롤러 끔(물리 캡슐이 탈것과 부딪치지 않게) → 탑승 자세 파라미터 1 → 운전석이면 탑승자의 조종자가 탈것 폰을 쥔다.
     *          승객석은 빙의를 옮기지 않습니다(승객은 자기 폰을 계속 쥐고 시선만 돌린다 — 이동은 멈춰 있다). 내리기는 역순입니다.
     */
    struct SW_GF_API MountUtil
    {
        /** @brief @p riderPawn 을 @p seat 에 태웁니다. */
        static MountResult mount( PawnComponent& riderPawn, VehicleSeatComponent& seat );
        /**
         * @brief 내립니다 — 하차 자리(막혀 있으면 좌석 둘레 여덟 방향 중 빈 곳)로 떼고, 이동을 켜고, 운전석이면 조종자가 탑승자 폰을 다시 쥡니다.
         * @param bForced 강제 하차(쓰러짐 · 말이 떨어뜨림)면 하차 자리를 찾지 않고 지금 자리에서 뗍니다.
         * @return 앉아 있지 않으면 false 입니다.
         */
        static bool dismount( PawnComponent& riderPawn, bool bForced );
        /** @brief 탑승자가 앉은 좌석입니다(없으면 nullptr). */
        static VehicleSeatComponent* findSeatOf( const PawnComponent& riderPawn );
        /** @brief @p riderPawn 에서 타는 거리 안의 빈 좌석 — 운전석 먼저, 그다음 가까운 순입니다. 없으면 nullptr 입니다. */
        static VehicleSeatComponent* findNearestFreeSeat( const PawnComponent& riderPawn );
    };
} // namespace sw
