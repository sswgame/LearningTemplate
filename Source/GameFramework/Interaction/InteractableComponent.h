/**
 * @file InteractableComponent.h
 * @brief 상호작용할 수 있는 오브젝트 — 상호작용 종류(데이터), 맞춤 지점, 쿨다운, 강조 요청 깃발입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Interaction/InteractionCatalog.h"

namespace sw
{
    /**
     * @class InteractableComponent
     * @brief 종류(`_interactionId`)는 상호작용 표(`_catalogPath`)에서 찾습니다. 코드로 정의를 넣으면(`setDefinition`) 그것이 이깁니다.
     * @details 맞춤 지점(`computeAlignmentPoint`)은 하는 쪽이 설 자리와 방향입니다 — 정의의 마커 이름(`_alignmentMarker`)을 이 오브젝트의 소켓 · 마커 표
     *          (`SocketSetComponent` 의 `*.sockets.xml`, 마커의 +Z 가 하는 쪽이 볼 방향)에서 찾고, 없으면 오브젝트 원점 · 앞입니다. 하는 쪽이 시작할 때
     *          그 자리를 마커 이름의 워프 목표로 넣어(`MotionWarpingComponent`) 애니메이션이 모션 워핑으로 손을 문고리에 맞춥니다.
     *          강조 요청은 고른 하는 쪽(`InteractorComponent`)이 세우고 내리는 원자 깃발이라 렌더러 · UI 가 아무 때나 읽습니다(`getHighlightRequest`).
     *          완료(`completeInteraction`)는 쿨다운을 걸고, 완료 수를 올리고(같은 오브젝트의 기믹 센서가 끌어 읽는다 — 상호작용은 기믹을 모른다),
     *          `InteractionCompletedEvent` 를 내고, 권한 훅에 알립니다. 게임 스레드에서 부릅니다(하는 쪽이 틱 뒤로 미룬다).
     */
    REFLECT( Category = "Interaction", DisplayName = "Interactable", Tooltip = "Something an interactor can use: data-defined kind, alignment point, cooldown, highlight" )
    class SW_GF_API InteractableComponent : public Component
    {
    public:
        REFLECT_BODY();

        InteractableComponent();
        virtual ~InteractableComponent() override = default;

        void onPostLoad() override;
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 정의를 코드로 넣습니다(표보다 우선). */
        void setDefinition( const InteractionDef& def );
        /** @brief 지금 정의입니다. 표에 없으면 nullptr 입니다. */
        const InteractionDef* getDefinition() const;
        /** @brief 켜져 있고 쿨다운이 끝났고 @p interactor 의 태그가 조건을 만족하는가입니다. */
        bool isAvailableFor( const GameObject& interactor ) const;
        /**
         * @brief 하는 쪽이 설 월드 자리와 방향(요, 라디안 — `atan2( 앞.x, 앞.z )`)입니다.
         * @return 정의의 마커를 소켓 표에서 찾았으면 true, 아니면 false 이고 오브젝트 원점 · 앞입니다.
         */
        bool computeAlignmentPoint( float3& outPosition, float32& outYaw ) const;

        void setInteractionId( const hashed_string& id );
        /** @brief 상호작용 표 경로를 바꾸고 정의를 다시 찾습니다. 빈 글이면 공용 기본표(`InteractionCatalog::kDefaultPath`)입니다. */
        void    setCatalogPath( string_view path );
        void    setEnabled( bool bEnabled ) { _bEnabled = bEnabled; }
        bool    isEnabled() const { return _bEnabled; }
        int32   getPriority() const { return _priority; }
        float32 getCooldownRemaining() const { return _cooldownRemaining; }

        /** @brief 강조를 요청하거나 거둡니다(아무 스레드). */
        void setHighlightRequested( bool bRequested ) { _bHighlightRequested.store( bRequested ? SW_TRUE : SW_FALSE, std::memory_order_relaxed ); }
        /** @brief 요청이 있으면 정의의 강조 방식, 없으면 None 입니다. 렌더러 · UI 가 읽습니다. */
        InteractionHighlight getHighlightRequest() const;

        /** @brief 끝났습니다 — 쿨다운 · 완료 수 · 이벤트 · 권한 훅. 게임 스레드에서 부릅니다. */
        void completeInteraction( const GameObject& interactor );
        /** @brief 지금까지 끝난 횟수입니다(아무 스레드). 기믹 센서가 지난번에 본 수와 견줘 사용으로 셉니다. */
        uint32 getCompletedCount() const { return _completedCount.load( std::memory_order_relaxed ); }
        /** @brief 마지막으로 끝낸 이입니다(밀기 방향 · 채집 보상 대상). */
        GameObjectHandle getLastInteractor() const { return _lastInteractor; }

    private:
        void resolveDefinition();

    private:
        PROPERTY( Category = "Interaction", DisplayName = "Interaction", Tooltip = "Interaction kind id in the catalog" )
        hashed_string _interactionId;
        PROPERTY( Category = "Interaction", DisplayName = "Catalog", AssetPath, Tooltip = "Interaction table; empty uses the common default" )
        string _catalogPath;
        PROPERTY( Category = "Interaction", DisplayName = "Last Interactor", Tooltip = "Who completed it last (runtime)" )
        GameObjectHandle _lastInteractor;
        PROPERTY( Category = "Interaction", DisplayName = "Cooldown Remaining", Tooltip = "Seconds until usable again (runtime)", Units = s )
        float32 _cooldownRemaining;
        PROPERTY( Category = "Interaction", DisplayName = "Priority", Tooltip = "Higher wins over nearer candidates" )
        int32 _priority;
        PROPERTY( Category = "Interaction", DisplayName = "Enabled" )
        bool _bEnabled;

        InteractionDef        _overrideDef;
        const InteractionDef* _pDef;
        uint32                _seenCatalogReloadCount; ///< 정의를 찾을 때의 `InteractionCatalog::getSharedReloadCount` — 달라지면 다시 찾는다
        atomic<uint32>        _completedCount;         ///< 끝난 횟수 — 같은 오브젝트의 기믹 센서가 끌어 읽는다
        atomic<uint8>         _bHighlightRequested;
        uint8                 _bHasOverride;
    };
} // namespace sw
