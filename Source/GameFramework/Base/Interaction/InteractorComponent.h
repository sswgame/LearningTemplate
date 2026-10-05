/**
 * @file InteractorComponent.h
 * @brief 상호작용하는 쪽(플레이어 · AI) — 대상 고르기(거리 · 시야각 · 시야), UI 안내 데이터, 입력 방식 진행, 강조 요청, 권한 훅입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Interaction/InteractionSelector.h"
#include "GameFramework/Base/Interaction/InteractionSession.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief UI 가 그리는 안내 한 벌입니다(문구 키 · 입력 방식 · 진행 · 단계). */
    struct InteractionPrompt
    {
        hashed_string        _prompt{};
        GameObjectHandle     _target{};
        float32              _progress{ 0.0f };
        int32                _stepIndex{ 0 };
        int32                _stepCount{ 0 };
        InteractionInputMode _mode{ InteractionInputMode::Press };
        uint8                _bVisible{ SW_FALSE };     ///< 고른 대상이 있다
        uint8                _bInteracting{ SW_FALSE }; ///< 진행 중이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class InteractorComponent
     * @brief 틱마다 씬의 `InteractableComponent` 중 쓸 수 있는(켜짐 · 쿨다운 · 태그) 것을 후보로 모아 `InteractionSelector` 로 고르고, 입력(`setInput`)으로
     *        `InteractionSession` 을 진행합니다. 2D 는 `_space = Space2D` 와 `setFacing2D`(좌우 보기)로 같은 코드가 돕니다.
     * @details 눈은 소유자 월드 자리 + `_eyeOffset`, 시선은 3D 면 소유자의 앞(+Z), 2D 면 `_facing2D` 입니다. 진행 중에는 대상을 바꾸지 않고, 대상이 쓸 수 없게
     *          되거나 거리의 1.25 배 밖으로 나가면 취소합니다. 완료는 틱 뒤에 대상의 `completeInteraction` 을 부릅니다(대상 상태를 게임 스레드에서 바꾼다).
     *          권한이 `Server` 인 상호작용은 권한 훅(`IInteractionAuthority`)이 걸려 있으면 시작 전에 허락을 묻습니다.
     */
    REFLECT( Category = "Interaction", DisplayName = "Interactor", Tooltip = "Picks the best interactable in reach and view, drives press / hold / mash input" )
    class SW_GF_API InteractorComponent : public Component
    {
    public:
        REFLECT_BODY();

        InteractorComponent();
        virtual ~InteractorComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 이번 프레임의 상호작용 버튼입니다(틱 전에 게임 입력이 넣는다). 새로 누른 것은 지난 값과 견줘 정한다. */
        void setInput( bool bHeld );
        /** @brief 2D 의 시선(좌우)입니다. */
        void setFacing2D( const float3& facing ) { _facing2D = facing; }
        void setSpace( InteractionSpace space ) { _space = space; }
        void setEyeOffset( const float3& eyeOffset ) { _eyeOffset = eyeOffset; }
        /** @brief 진행 중인 상호작용을 취소합니다(맞음 · 컷신). */
        void cancelInteraction();

        const InteractionPrompt&  getPrompt() const { return _prompt; }
        GameObjectHandle          getFocus() const { return _focus; }
        bool                      isInteracting() const { return _session.isActive(); }
        const InteractionSession& getSession() const { return _session; }

    private:
        void makeViewer( InteractionViewer& outViewer ) const;
        void gatherCandidates( GameObjectManager& manager, const GameObject& owner, const InteractionViewer& viewer );
        void setFocus( GameObjectManager& manager, GameObjectHandle focus, ComponentHandle focusComponent );
        void finishSession( GameObjectManager& manager, const GameObject& owner );

    private:
        PROPERTY( Category = "Interaction", DisplayName = "Space", Tooltip = "3D distance and view, or the 2D XY plane" )
        InteractionSpace _space;
        PROPERTY( Category = "Interaction", DisplayName = "Eye Offset", Tooltip = "Eye point above the owner origin", Units = m )
        float3 _eyeOffset;
        PROPERTY( Category = "Interaction", DisplayName = "Facing 2D", Tooltip = "View direction in 2D (left or right)" )
        float3 _facing2D;
        PROPERTY( Category = "Interaction", DisplayName = "Line Of Sight", Tooltip = "Ask the physics world whether the target is visible" )
        bool _bUseLineOfSight;

        InteractionSession              _session;
        InteractionPrompt               _prompt;
        vector<InteractionCandidate>    _listCandidate;
        vector<ComponentHandle>         _listCandidateComponent;
        vector<InteractionSessionEvent> _listSessionEvent;
        GameObjectHandle                _focus;
        ComponentHandle                 _focusComponent;
        uint8                           _bHeld;
        uint8                           _bWasHeld;
        uint8                           _bPressed;
    };
} // namespace sw
