/**
 * @file VoxelHighlightComponent.h
 * @brief 플레이어가 바라보는 블록을 표시합니다. 반투명 큐브가 겨눈 블록에 겹쳐 보이고, 블록을 부수는 동안 네 단계로 진해집니다. 에디터 게임 뷰에서는 테두리도 그립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class MaterialInstance;

    /**
     * @class VoxelHighlightComponent
     * @brief 하이라이트 오브젝트(반투명 큐브 메시)의 컴포넌트입니다. `TickGroup::PostUpdate` 에서 플레이어 컴포넌트가 겨눈 블록과 부수기 진행도를 읽고, 자기 메시에만 씁니다.
     * @details 머티리얼 인스턴스는 단계마다 하나씩 플레이 시작 때 게임 스레드에서 만듭니다. 이렇게 하면 인스턴스가 계속 늘어나지 않습니다.
     *          테두리는 디버그 선이라서 틱이 끝난 뒤 게임 스레드에서 그립니다.
     */
    REFLECT( Category = "VoxelCraft", DisplayName = "Voxel Highlight", Tooltip = "Translucent box over the block the player aims at" )
    class VoxelHighlightComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kLevelCount = 4;

        VoxelHighlightComponent();
        virtual ~VoxelHighlightComponent() override;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        /** @brief 겨눈 블록의 테두리(디버그 선)를 틱이 끝난 뒤 게임 스레드에서 그립니다. */
        void scheduleOutline( const float3& center ) const;

    private:
        PROPERTY( Category = "Highlight", DisplayName = "Player", Tooltip = "Object with the VoxelPlayerComponent" )
        GameObjectHandle _player;

        shared_ptr<MaterialInstance> _arrLevelLook[kLevelCount];
        int32                        _level; ///< 지금 적용한 단계입니다(−1 이면 아직 적용하지 않았습니다).
    };
} // namespace sw
