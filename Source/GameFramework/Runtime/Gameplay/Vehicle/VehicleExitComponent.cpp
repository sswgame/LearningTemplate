#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    /**
     * @class VehicleExitComponent
     * @brief 탈것 오브젝트(폰과 운전석)에 붙습니다. PrePhysics 틱에서 탈것 폰의 의도만 읽고, 내리기는 틱 뒤로 미룹니다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Vehicle Exit", Tooltip = "Vehicle side: the Exit intent button gets the driver off" )
    class VehicleExitComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics;

        VehicleExitComponent();
        ~VehicleExitComponent() override = default;

        /** @brief 버튼 이름의 인덱스를 찾고 PrePhysics 에서 틱합니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤에 실행합니다. 운전석 탑승자를 내립니다. */
        void exitDriver();

    private:
        PROPERTY( Required, Category = "Vehicle", DisplayName = "Exit Button", Tooltip = "Intent button of the vehicle pawn that gets the driver off" )
        hashed_string _exitButton;

        SW_TRANSIENT optional<int32> _exitIndex;
    };

    VehicleExitComponent::VehicleExitComponent()
        : _exitButton{}
        , _exitIndex{}
    {
        setCanEverTick( true );
    }

    void VehicleExitComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _exitIndex                  = pPawn != nullptr ? pPawn->findButton( _exitButton ) : nullopt;
    }

    void VehicleExitComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*          pOwner       = getOwner();
        const PawnComponent* pPawn        = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        const bool           bNoExitInput = pPawn == nullptr || pPawn->wasButtonTriggered( _exitIndex ) == false;
        if ( bNoExitInput )
            return;
        context.deferPostTickOnComponent( *this, []( VehicleExitComponent& component )
        { component.exitDriver(); } );
    }

    void VehicleExitComponent::exitDriver()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager == nullptr || pManager->getSubsystem<SceneNetLink>().hasAuthority() == false )
            return; // 접속한 클라이언트에서는 서버의 탈것이 내린다
        // 이 탈것의 운전석을 좌석 레지스트리에서 찾는다. 씬 전체를 순회하지 않는다.
        for ( VehicleSeatComponent* pSeat : pManager->getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            const bool bSkipSeat = pSeat == nullptr || pSeat->getOwner() != pOwner || pSeat->isDriverSeat() == false || pSeat->isFree();
            if ( bSkipSeat )
                continue;
            GameObject*    pRider     = pManager->resolveGameObject( pSeat->getOccupant() );
            PawnComponent* pRiderPawn = pRider != nullptr ? pRider->getComponent<PawnComponent>() : nullptr;
            if ( pRiderPawn != nullptr )
                (void)VehicleSeatComponent::dismount( *pRiderPawn, false );
            return;
        }
    }
} // namespace sw

#include "VehicleExitComponent.cpp.gen.inl"
