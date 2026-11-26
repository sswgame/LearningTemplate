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
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityBuildInputComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameFrameworkComponent.h"

namespace sw
{
    /**
     * @class CityBuildCursorComponent
     * @brief `TickGroup::PostUpdate` 에서 짓기 입력(디렉터와 같은 오브젝트 — `_gameMode`)을 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 커서가 땅 밖이면 숨깁니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "City Build Cursor", Tooltip = "Shows the build cursor of the City Build Input on the game mode object" )
    class CityBuildCursorComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 짓기 입력은 틱 뒤에 커서 칸을 정한다 — 지난 프레임 것을 읽는다.

        CityBuildCursorComponent();
        virtual ~CityBuildCursorComponent() override = default;

        void onTick( TickContext& context ) override;

    private:
        PROPERTY( Category = "Cursor", DisplayName = "Height", Tooltip = "Box centre above the ground", Units = m )
        float32 _height;
        PROPERTY( Category = "Cursor", DisplayName = "Thickness", Tooltip = "Box height", Units = m )
        float32 _thickness;
    };

    CityBuildCursorComponent::CityBuildCursorComponent()
        : _height{ 0.1f }
        , _thickness{ 0.12f }
    {
        setCanEverTick( true );
    }

    void CityBuildCursorComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const CityBuildInputComponent* pBuildInput = findGameMode<CityBuildInputComponent>();
        int2                           tile{};
        int32                          size   = 1;
        const bool                     bShown = pBuildInput != nullptr && pBuildInput->findCursor( tile, size );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        const float32 extent = static_cast<float32>( size );
        const float3  center{ static_cast<float32>( tile._x ) + extent * 0.5f, _height, static_cast<float32>( tile._y ) + extent * 0.5f };
        pMesh->setLocalPosition( pOwner->getManager()->getWorldOrigin().toLocal( center ) );
        pMesh->setLocalScale( float3{ extent, _thickness, extent } );
    }
} // namespace sw

#include "CityBuildCursorComponent.cpp.gen.inl"
