/**
 * @file MountUtil.h
 * @brief 타기와 내리기입니다. 탑승은 조종자가 빙의를 탈것으로 옮기는 일입니다. 탑승자 폰은 좌석 소켓에 붙고 이동이 멈춥니다. 플레이어와 NPC 가 같은 함수로 탑니다.
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
        SeatTaken,    ///< 누가 앉아 있거나, 탑승자가 이미 어딘가에 앉아 있다
        TooFar,       ///< 좌석의 타는 거리 밖이다
        NoSocket,     ///< 탈것 외형에 좌석 소켓이 없거나 붙일 수 없다
        NotPossessed, ///< 운전석인데 탑승자를 쥔 조종자가 없다. 넘겨줄 빙의가 없는 경우이다
        NoVehiclePawn ///< 운전석인데 탈것에 폰이 없다
    };
} // namespace sw

namespace sw
{
    class PawnComponent;
    class VehicleSeatComponent;

    /**
     * @struct MountUtil
     * @brief 타기와 내리기를 처리합니다. 게임 스레드의 틱 밖에서 부릅니다. 틱 안이면 `GameObjectManager::executeOrDeferPostTick` 으로 미루며, `MountInteractionComponent` 가 그렇게 합니다.
     * @details 언리얼에는 기본 틀이 없고, 관례가 "탈것 폰 + `Possess` + 탑승자 `AttachToComponent( Seat socket )` + 탑승자 이동 끔" 이다. 이 관례를 기반 코드로 구현했다.
     *          타기 순서는 다음과 같다. 먼저 좌석과 거리를 확인한다. 그다음 소켓 변환을 구하고(탈것 외형의 소켓, 없으면 좌석 오프셋), 탑승자를 `SocketBindingComponent::bindToSocket` 으로 붙인다.
     *          폰 이동을 멈추고 캐릭터 컨트롤러를 끈다(물리 캡슐이 탈것과 부딪치지 않게). 탑승 자세 파라미터를 1 로 둔다. 운전석이면 탑승자의 조종자가 탈것 폰을 쥔다.
     *          승객석은 빙의를 옮기지 않는다. 승객은 자기 폰을 계속 쥐고 시선만 돌리며, 이동은 멈춰 있다. 내리기는 역순이다.
     */
    struct SW_GF_API MountUtil
    {
        /** @brief @p riderPawn 을 @p seat 에 태웁니다. */
        static MountResult mount( PawnComponent& riderPawn, VehicleSeatComponent& seat );
        /**
         * @brief 내립니다. 하차 위치(막혀 있으면 좌석 둘레 여덟 방향 중 빈 곳)로 분리하고, 이동을 켜고, 운전석이면 조종자가 탑승자 폰을 다시 쥐게 합니다.
         * @param bForced 강제 하차(쓰러짐, 말이 떨어뜨림)이면 하차 위치를 찾지 않고 지금 위치에서 분리합니다.
         * @return 앉아 있지 않으면 false 입니다.
         */
        static bool dismount( PawnComponent& riderPawn, bool bForced );
        /** @brief 탑승자가 앉은 좌석입니다. 없으면 nullptr 입니다. */
        static VehicleSeatComponent* findSeatOf( const PawnComponent& riderPawn );
        /** @brief @p riderPawn 이 타는 거리 안에 있는 빈 좌석입니다. 운전석을 먼저 보고, 그다음은 가까운 순입니다. 없으면 nullptr 입니다. */
        static VehicleSeatComponent* findNearestFreeSeat( const PawnComponent& riderPawn );
    };
} // namespace sw
