#include "pch.h"

#include "Core/Foundation/Types.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSCommandInputComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameFrameworkComponent.h"

namespace sw
{
    /**
     * @class RTSDragBoxComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터 오브젝트의 명령 입력을 읽기만 하고 자기 오브젝트의 메시에만 씁니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "RTS Drag Box", Tooltip = "Shows the drag-select box of the RTS Command Input on the game mode object" )
    class RTSDragBoxComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 명령 입력은 틱 뒤에 끌기를 정한다 — 여기서 읽는 것은 지난 프레임의 끌기다.

        RTSDragBoxComponent();
        virtual ~RTSDragBoxComponent() override = default;

        void onTick( TickContext& context ) override;
    };

    RTSDragBoxComponent::RTSDragBoxComponent()
    {
        setCanEverTick( true );
    }

    void RTSDragBoxComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const GameObject* const               pDirectorObject = context.getManager().resolveGameObject( getGameMode() );
        const RTSCommandInputComponent* const pCommandInput   = pDirectorObject != nullptr ? pDirectorObject->getTickSafeComponent<RTSCommandInputComponent>() : nullptr;
        float3                                center{};
        float3                                scale{};
        const bool                            bShown = pCommandInput != nullptr && pCommandInput->findDragBox( center, scale );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        pMesh->setLocalPosition( context.getManager().getWorldOrigin().toLocal( center ) ); // 끌기 자리는 RTS 월드의 절대 자리다
        pMesh->setLocalScale( scale );
    }
} // namespace sw

#include "RTSDragBoxComponent.cpp.gen.inl"
