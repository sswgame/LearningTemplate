/**
 * @file InteractorComponent.h
 * @brief 상호작용하는 쪽(플레이어, AI)입니다. 대상 고르기(거리, 시야각, 시야), UI 안내 데이터, 입력 방식에 따른 진행, 강조 요청, 권한 훅을 처리합니다.
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

#include "GameFramework/Base/Gameplay/Interaction/InteractionSelector.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionSession.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief UI 가 그리는 안내 데이터 한 벌입니다. 문구 키, 입력 방식, 진행량, 단계가 들어 있습니다. */
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
    class InteractableComponent;

    /**
     * @class InteractorComponent
     * @brief 틱마다 씬의 `InteractableComponent` 중 쓸 수 있는 것(켜짐, 쿨다운, 태그 조건)을 후보로 모아 `InteractionSelector` 로 고르고, 입력(`setInput`)으로
     *        `InteractionSession` 을 진행합니다. 2D 는 `_space = Space2D` 와 `setFacing2D`(좌우 보기)를 쓰며 같은 코드가 동작합니다.
     * @details 눈의 위치는 소유자의 월드 위치에 `_eyeOffset` 을 더한 값이고, 시선은 3D 이면 소유자의 앞(+Z), 2D 이면 `_facing2D` 이다. 진행 중에는 대상을 바꾸지 않고, 대상이 쓸 수 없게
     *          되거나 거리의 1.25 배 밖으로 나가면 취소한다. 완료하면 틱 뒤에 대상의 `completeInteraction` 을 부른다. 대상 상태를 게임 스레드에서 바꾸기 위해서다.
     *          권한이 `Server` 인 상호작용은 권한 훅(`IInteractionAuthority`)이 등록되어 있으면 시작 전에 허락을 묻는다.
     *          단계에 페어드 애니메이션 세트가 있으면 단계가 시작될 때 틱 뒤에 하는 쪽과 대상의 애니메이터로 세션을 시작하고(`PairedAnimationSystem`), 거절되면 상호작용을
     *          취소한다. 상호작용을 취소하면 그 세션도 중단하고, 완료하면 세션은 자기 타임라인 끝까지 간다.
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

        /** @brief 이번 프레임의 상호작용 버튼 상태입니다. 틱 전에 게임 입력이 넣습니다. 새로 눌렀는지는 지난 값과 비교해서 정합니다. */
        void setInput( bool bHeld );
        /** @brief 2D 의 시선(좌우)입니다. */
        void setFacing2D( const float3& facing ) { _facing2D = facing; }
        void setSpace( InteractionSpace space ) { _space = space; }
        void setEyeOffset( const float3& eyeOffset ) { _eyeOffset = eyeOffset; }
        /** @brief 진행 중인 상호작용을 취소합니다. 맞았을 때나 컷신에서 씁니다. */
        void cancelInteraction();

        const InteractionPrompt&  getPrompt() const { return _prompt; }
        GameObjectHandle          getFocus() const { return _focus; }
        bool                      isInteracting() const { return _session.isActive(); }
        const InteractionSession& getSession() const { return _session; }

    private:
        void makeView( InteractionView& outViewer ) const;
        void gatherCandidates( GameObjectManager& manager, const GameObject& owner, const InteractionView& viewer );
        void setFocus( GameObjectManager& manager, GameObjectHandle focus, TComponentHandle<InteractableComponent> focusComponent );
        /** @brief 진행 중인 상호작용을 이어 갑니다. 진행 중에는 대상을 바꾸지 않고, 대상을 쓸 수 없게 됐거나 멀어지면 취소합니다. */
        void updateActiveSession( GameObjectManager& manager, const InteractionView& viewer, float32 deltaTime, bool bHeld, bool bPressed );
        /** @brief 가장 알맞은 후보로 초점을 옮기고, 눌렀으면 권한을 확인한 뒤 상호작용을 시작합니다. */
        void updateFocusAndBegin( GameObjectManager& manager, GameObject& owner, const InteractionView& viewer, bool bPressed );
        /**
         * @brief 세션 이벤트마다 몽타주와 페어드 애니메이션을 처리합니다.
         * @details 몽타주는 단계가 시작될 때 튼다. 시작은 첫 단계이고, 마지막이 아닌 단계를 끝내면 다음 단계다. 취소하면 끊고 그 전 상태로 돌아간다.
         */
        void handleSessionEvents( GameObjectManager& manager, GameObject& owner );
        /** @brief 화면 안내(`_prompt`)를 초점과 진행 단계로 다시 채웁니다. 진행 중이 아니면 첫 단계의 안내를 보입니다. */
        void updatePrompt( GameObjectManager& manager );
        void finishSession( GameObjectManager& manager, const GameObject& owner );
        /** @brief 단계의 페어드 애니메이션 세트를 틱 뒤(게임 스레드)에 시작하도록 미룹니다. */
        void deferPairedAnimationStart( GameObjectManager& manager, const InteractionStepDef& step );
        /**
         * @brief 하는 쪽(`interactorRole`)과 대상(`interactableRole`)의 애니메이터로 배치에 맞는 첫 세트를 시작합니다(게임 스레드, 틱 밖).
         * @details 애니메이터나 에셋이 없거나 시작이 거절되면 상호작용을 취소한다. 애니메이션 없이 진행하지 않는다.
         */
        void startPairedAnimation( GameObjectManager& manager, TComponentHandle<InteractableComponent> target, const InteractionStepDef& step );
        /** @brief 이 상호작용이 시작한 페어드 애니메이션이 있으면 틱 뒤에 중단합니다. */
        void deferPairedAnimationAbort( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Interaction", DisplayName = "Space", Tooltip = "3D distance and view, or the 2D XY plane" )
        InteractionSpace _space;
        PROPERTY( Category = "Interaction", DisplayName = "Eye Offset", Tooltip = "Eye point above the owner origin", Units = m )
        float3 _eyeOffset;
        PROPERTY( Category = "Interaction", DisplayName = "Facing 2D", Tooltip = "View direction in 2D (left or right)" )
        float3 _facing2D;
        PROPERTY( Category = "Interaction", DisplayName = "Line Of Sight", Tooltip = "Ask the physics world whether the target is visible" )
        bool _bUseLineOfSight;
        PROPERTY( Category = "Interaction", DisplayName = "Keep Distance Scale", Tooltip = "A focused target stays focused until it is this many times its reach away (hysteresis)", Min = 1.0 )
        float32 _keepDistanceScale;

        InteractionSession                              _session;
        hashed_string                                   _resumeState;     ///< 몽타주를 틀기 전 애니메이터 상태입니다. 취소하면 이 상태로 돌아갑니다(몽타주가 없었으면 비어 있다).
        int32                                           _montageStep;     ///< 이번 상호작용에서 몽타주를 튼 단계 번호입니다(상호작용이 아니면 -1).
        uint32                                          _pairedSessionID; ///< 이번 상호작용이 시작한 페어드 애니메이션 세션 id 입니다(없으면 0). 취소하면 중단한다
        InteractionPrompt                               _prompt;
        vector<InteractionCandidate>                    _listCandidate;
        vector<TComponentHandle<InteractableComponent>> _listCandidateComponent;
        vector<InteractionSessionEvent>                 _listSessionEvent;
        GameObjectHandle                                _focus;
        TComponentHandle<InteractableComponent>         _focusComponent;
        uint8                                           _bHeld;
        uint8                                           _bWasHeld;
        uint8                                           _bPressed;
        uint8                                           _bPairedStartPending; ///< 페어드 애니메이션 시작을 틱 뒤로 미뤘다. 그 사이 취소되면 내려 시작하지 않는다
    };
} // namespace sw
