/**
 * @file VoxelHighlightComponent.h
 * @brief 플레이어가 바라보는 블록을 표시합니다. 반투명 큐브가 겨눈 블록에 겹쳐 보이고, 블록을 부수는 동안 네 단계로 진해집니다. 테두리는 모서리마다 얇은 상자 프리팹으로 보입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

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
    class SW_KIT_CLIENT_API VoxelHighlightComponent : public Component
    {
    public:
        REFLECT_BODY();

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

        shared_ptr<MaterialInstance> _arrLevelLook[kLevelCount];
        PrefabViewBatch              _outlineViews;
        AbsolutePosition             _targetCenter;  ///< 이번 틱에 겨눈 블록의 가운데(절대 자리)
        int32                        _level;         ///< 지금 적용한 단계입니다(−1 이면 아직 적용하지 않았습니다).
        uint8                        _bTargeted : 1; ///< 이번 틱에 겨눈 블록이 있다
        [[maybe_unused]] uint8       _reserved  : 7;
    };
} // namespace sw
