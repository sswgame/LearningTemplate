/**
 * @file VisualSinkReactionComponent.h
 * @brief 반응 — 속성(기본 `SinkDepth`)만큼 외형을 내립니다. 늪 · 모래의 가라앉음이 이펙트 스택으로 속성을 올리면, 이 컴포넌트가 몸 · 모자 · 장비를 같은 깊이로 내린다.
 * @details 반응은 속성 · 태그만 읽는 작은 컴포넌트다 — 어느 지역이 올렸는지 모른다. 내리는 메시는 **다른 메시에 매달리지 않은 메시 전부**다(외형 부품은 몸의 자식이 아니라
 *          따로 붙은 메시일 수 있다 — 첫 메시만 내리면 모자가 지면에 남는다. 매달린 것은 부모를 따라 내려가므로 두 번 내리지 않는다). 처음 내릴 때 원래 로컬 자리를 적고,
 *          속성이 0 이 되거나 컴포넌트가 끝날 때 되돌린다(겹쳐 쌓이지 않는다). 캡슐은 지면에 남는다(Jolt 캐릭터는 정적 바닥 아래로 갈 수 없다).
 *          shortcut: 빠진 캐릭터를 쏘거나 베는 판정이 깊이를 따라야 하면 캡슐 반 높이를 깊이 단계로 줄인다. 외형이 다시 조립되어 메시 수가 바뀌면 원래 자리를 다시 적는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class SceneComponent;

    /**
     * @class VisualSinkReactionComponent
     * @brief 속성만큼 외형을 내리는 반응입니다(파일 머리말 참고).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Visual Sink Reaction", Tooltip = "Lowers every root mesh of the object by an ability attribute (SinkDepth): swamps and quicksand sink the whole look" )
    class SW_GF_API VisualSinkReactionComponent : public Component
    {
    public:
        REFLECT_BODY();

        VisualSinkReactionComponent();
        ~VisualSinkReactionComponent() override = default;

        void onTick( TickContext& context ) override;
        /** @brief 메시를 원래 자리로 되돌립니다(씬 전환 · 컴포넌트 제거에서 표현이 지하에 남지 않게). */
        void onEndPlay() override;

        /** @brief 지금 내린 깊이(m)입니다(감시 값 · 진단). */
        float32 getAppliedDepth() const { return _appliedDepth; }
        /** @brief 내리는 메시 수입니다(감시 값 · 진단). */
        uint32 getLoweredMeshCount() const { return static_cast<uint32>( _listVisual.size() ); }
        /** @brief 속성을 읽어 자기 메시를 내립니다(PostPhysics 틱 — 이번 프레임 지역 처리 뒤, 테스트도 부른다). */
        void applySinkDepth( GameObjectManager& manager );

    private:
        /** @brief 내리는 메시 하나와 원래 로컬 자리입니다. */
        struct SunkVisual
        {
            TComponentHandle<MeshComponent> _mesh;
            float3                          _localPosition;
        };

        /** @brief @p component 위로 메시가 있는지 봅니다(있으면 그 메시를 따라 내려간다). */
        [[nodiscard]] static bool hasMeshAncestor( const SceneComponent& component );
        /** @brief 내린 메시를 원래 자리로 되돌리고 목록을 비웁니다. */
        void restoreVisuals( GameObjectManager& manager );

    private:
        PROPERTY( Required, Category = "Reaction", DisplayName = "Attribute", Tooltip = "Ability attribute read as the sink depth" )
        hashed_string _attribute;
        PROPERTY( Category = "Reaction", DisplayName = "Scale", Min = 0.0, Units = m, Tooltip = "Metres lowered per attribute unit" )
        float32 _scale;

        vector<SunkVisual> _listVisual;
        float32            _appliedDepth;
    };
} // namespace sw
