/**
 * @file VoxelHighlightComponent.h
 * @brief 바라보는 블록 표시 — 반투명 큐브가 플레이어가 겨눈 블록에 겹치고, 부수는 동안 네 단계로 진해집니다(에디터 게임 뷰에는 테두리도).
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
     * @brief 하이라이트 오브젝트(반투명 큐브 메시)의 컴포넌트입니다. `TickGroup::PostUpdate` 에서 플레이어 컴포넌트의 겨눈 블록 · 부수기 진행을 읽고 자기 메시에만 씁니다.
     * @details 단계마다 머티리얼 인스턴스 하나(플레이 시작에 게임 스레드에서 만든다 — 인스턴스가 끝없이 늘지 않게). 테두리는 디버그 선이라 틱 뒤 게임 스레드에서 낸다.
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
        /** @brief 겨눈 블록의 테두리(디버그 선)를 틱 뒤 게임 스레드에서 냅니다. */
        void scheduleOutline( const float3& center ) const;

    private:
        PROPERTY( Category = "Highlight", DisplayName = "Player", Tooltip = "Object with the VoxelPlayerComponent" )
        GameObjectHandle _player;

        shared_ptr<MaterialInstance> _arrLevelLook[kLevelCount];
        int32                        _level; ///< 지금 입은 단계(−1 이면 아직 없다)
    };
} // namespace sw
