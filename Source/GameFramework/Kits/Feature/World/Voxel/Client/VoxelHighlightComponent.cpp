#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelPlayerComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class GameObjectManager;
    class MaterialInstance;

    /**
     * @class VoxelHighlightComponent
     * @brief 하이라이트 오브젝트(반투명 큐브 메시)의 컴포넌트입니다. `TickGroup::PostUpdate` 에서 플레이어 컴포넌트가 겨눈 블록과 부수기 진행도를 읽고, 자기 메시에만 씁니다.
     * @details 머티리얼 인스턴스는 단계마다 하나씩 플레이 시작 때 게임 스레드에서 만듭니다. 이렇게 하면 인스턴스가 계속 늘어나지 않습니다.
     *          테두리는 다른 오브젝트(프리팹 인스턴스)라서 틱이 끝난 뒤 게임 스레드에서 보입니다.
     */
    REFLECT( Category = "Voxel", DisplayName = "Voxel Highlight", Tooltip = "Translucent box over the block the player aims at" )
    class VoxelHighlightComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 플레이어(DuringPhysics)가 이번 프레임에 겨눈 블록을 정한 뒤에 읽습니다.

        static constexpr int32 kLevelCount = 4;

        VoxelHighlightComponent();
        virtual ~VoxelHighlightComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱이 적은 겨눈 블록의 모서리 열둘을 얇은 상자로 보입니다(틱 뒤 게임 스레드). 겨눈 블록이 없으면 끕니다. */
        void showOutline( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Highlight", DisplayName = "Player", Tooltip = "Object with the VoxelPlayerComponent" )
        GameObjectHandle _player;
        PROPERTY( Category = "Highlight", DisplayName = "Color", Tooltip = "Box color before the block starts to break" )
        float4 _color;
        PROPERTY( Category = "Highlight", DisplayName = "Color Per Level", Tooltip = "Added to the color each break level" )
        float4 _colorPerLevel;
        PROPERTY( Category = "Highlight", DisplayName = "Outline Prefab", Required, Tooltip = "1 m cube stretched into the outline edges" )
        AssetRef<PrefabAsset> _outlinePrefab;
        PROPERTY( Category = "Highlight", DisplayName = "Outline Color" )
        float4 _outlineColor;
        PROPERTY( Category = "Highlight", DisplayName = "Outline Half Extent", Tooltip = "Half size of the outline box; above 0.5 keeps it outside the block", Min = 0.01, Units = m )
        float32 _outlineHalfExtent;
        PROPERTY( Category = "Highlight", DisplayName = "Outline Width", Tooltip = "Thickness of one outline edge", Min = 0.001, Units = m )
        float32 _outlineWidth;

        SW_TRANSIENT shared_ptr<MaterialInstance> _arrLevelLook[kLevelCount];
        SW_TRANSIENT PrefabViewBatch              _outlineViews;
        SW_TRANSIENT AbsolutePosition             _targetCenter;  ///< 이번 틱에 겨눈 블록의 가운데(절대 자리)
        SW_TRANSIENT int32                        _level;         ///< 지금 적용한 단계입니다(−1 이면 아직 적용하지 않았습니다).
        SW_TRANSIENT uint8                        _bTargeted : 1; ///< 이번 틱에 겨눈 블록이 있다
        [[maybe_unused]] uint8                    _reserved  : 7;
    };

    VoxelHighlightComponent::VoxelHighlightComponent()
        : _player{}
        , _color{ 1.0f, 1.0f, 1.0f, 0.18f }
        , _colorPerLevel{ 0.0f, -0.2f, -0.2f, 0.15f }
        , _outlinePrefab{}
        , _outlineColor{ 0.05f, 0.05f, 0.05f, 1.0f }
        , _outlineHalfExtent{ 0.505f }
        , _outlineWidth{ 0.015f }
        , _arrLevelLook{}
        , _outlineViews{}
        , _targetCenter{}
        , _level{ -1 }
        , _bTargeted{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    VoxelHighlightComponent::~VoxelHighlightComponent() = default;

    void VoxelHighlightComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        // 단계마다 하나씩 만듭니다. 블록을 부수는 동안 단계가 올라가며 색이 진해집니다. 플레이 시작은 틱 밖이라서 여기서 만듭니다.
        for ( int32 level = 0; level < kLevelCount && pMesh->getMaterial() != nullptr; ++level )
        {
            const float32 step   = static_cast<float32>( level );
            _arrLevelLook[level] = MaterialInstance::create( pMesh->getMaterial() );
            if ( _arrLevelLook[level] != nullptr )
                _arrLevelLook[level]->setVectorParameter( hashed_string( kMaterialColorParameter ), _color + _colorPerLevel * step );
        }
        _level = -1;
        pMesh->setVisible( false );
    }

    void VoxelHighlightComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _outlineViews.destroyAll( *pManager );
        Component::onEndPlay();
    }

    void VoxelHighlightComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*                 pOwner  = getOwner();
        MeshComponent*              pMesh   = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const GameObject*           pObject = context.getManager().resolveGameObject( _player );
        const VoxelPlayerComponent* pPlayer = pObject != nullptr ? pObject->getComponent<VoxelPlayerComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        VoxelCoord block{};
        const bool bHasTarget = pPlayer != nullptr && pPlayer->findTarget( block );
        if ( pMesh->isVisible() != bHasTarget )
            pMesh->setVisible( bHasTarget );
        // 블록은 절대 자리다 — 메시도 테두리도 원점으로 옮긴다
        _targetCenter = AbsolutePosition{
            float3{ static_cast<float32>( block._x ) + 0.5f, static_cast<float32>( block._y ) + 0.5f, static_cast<float32>( block._z ) + 0.5f }
        };
        _bTargeted = bHasTarget ? SW_TRUE : SW_FALSE;
        context.deferPostTickOnComponent( *this, []( VoxelHighlightComponent& highlight, GameObjectManager& manager )
        { highlight.showOutline( manager ); } );
        if ( bHasTarget == false )
            return;
        pMesh->setLocalPosition( context.getManager().getWorldOrigin().toLocal( _targetCenter ) );
        const int32 level = static_cast<int32>( MathUtil::clamp( pPlayer->getBreakProgress(), 0.0f, 0.99f ) * static_cast<float32>( kLevelCount ) );
        if ( level != _level && _arrLevelLook[level] != nullptr )
        {
            _level = level;
            pMesh->setMaterialInstance( _arrLevelLook[level] );
        }
    }

    void VoxelHighlightComponent::showOutline( GameObjectManager& manager )
    {
        _outlineViews.begin();
        if ( _bTargeted == SW_TRUE )
        {
            const float32 length = _outlineHalfExtent * 2.0f + _outlineWidth; // 모서리가 꼭짓점에서 맞물리게 굵기만큼 길다
            for ( int32 edge = 0; edge < 4; ++edge )
            {
                const float32 sideA = ( edge & 1 ) != 0 ? _outlineHalfExtent : -_outlineHalfExtent;
                const float32 sideB = ( edge & 2 ) != 0 ? _outlineHalfExtent : -_outlineHalfExtent;
                (void)_outlineViews.show( manager, _outlinePrefab, _targetCenter + float3{ 0.0f, sideA, sideB }, float3{ length, _outlineWidth, _outlineWidth }, 0.0f, _outlineColor );
                (void)_outlineViews.show( manager, _outlinePrefab, _targetCenter + float3{ sideA, 0.0f, sideB }, float3{ _outlineWidth, length, _outlineWidth }, 0.0f, _outlineColor );
                (void)_outlineViews.show( manager, _outlinePrefab, _targetCenter + float3{ sideA, sideB, 0.0f }, float3{ _outlineWidth, _outlineWidth, length }, 0.0f, _outlineColor );
            }
        }
        _outlineViews.end( manager );
    }
} // namespace sw

#include "VoxelHighlightComponent.cpp.gen.inl"
