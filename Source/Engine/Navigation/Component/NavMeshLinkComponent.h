/**
 * @file NavMeshLinkComponent.h
 * @brief 내비메시가 연결하지 못한 두 점(사다리 · 점프 · 문)을 연결하는 오프 메시 링크입니다. 언리얼 NavLink Proxy(Smart Link) · 유니티 `NavMeshLink` 에 해당합니다.
 * @details 두 끝은 오브젝트 기준 오프셋(`_startOffset` · `_endOffset`)이고, 베이크할 때 월드로 옮겨 기하(`NavMeshGeometry::addOffMeshLink`)에 들어갑니다 —
 *          정적 기하와 같이 쿠킹본에 포함되고, 속성을 바꾸거나 생기고 사라지면 그 위치의 타일을 다시 베이크합니다. 오브젝트를 옮긴 것은 따라가지 않습니다.
 *
 *          **애니메이션 신호.** 에이전트가 링크를 건너는 동안 `NavMeshAgentComponent::isOnOffMeshLink` 가 참이고, `getOffMeshLinkType` 이 이 링크의
 *          `_linkType`(예: `Ladder` · `Jump` · `Door`)을 돌려줍니다. 게임은 그 이름으로 동작을 고르고 진행률로 맞춥니다.
 *
 *          **막기.** `setLinkEnabled( false )` 면 경로가 이 링크를 지나지 않습니다(닫힌 문). 다음 내비게이션 갱신에 모든 에이전트 종류의 내비메시에 적용됩니다.
 *          링크 번호(`computeLinkID`)는 두 끝의 월드 위치와 종류 이름에서 나오므로 쿠킹본과 런타임 베이크가 같은 번호를 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct NavMeshSettings;
    struct NavOffMeshLink;

    class ValidationContext;

    /** @class NavMeshLinkComponent @brief 파일 머리말 참고. */
    REFLECT( Category = "Navigation", DisplayName = "NavMesh Link", Tooltip = "Off-mesh link between two points the navmesh does not join (ladder, jump, door)" )
    class SW_API NavMeshLinkComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavMeshLinkComponent();
        ~NavMeshLinkComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /**
         * @brief 지금 오브젝트 자세의 링크(월드)를 채웁니다. 영역 이름은 @p settings 의 번호로 바꾸고, 비었으면 @p fallbackArea 입니다.
         * @return 오브젝트에 씬 컴포넌트가 없으면 false 입니다.
         */
        [[nodiscard]] bool makeLink( const NavMeshSettings& settings, uint8 fallbackArea, NavOffMeshLink& outLink ) const;
        /** @brief 두 끝의 월드 위치입니다. 오브젝트에 씬 컴포넌트가 없으면 false 입니다. */
        [[nodiscard]] bool computeWorldEndpoints( float3& outStart, float3& outEnd ) const;
        /** @brief 지금 자세의 링크 번호입니다(씬 컴포넌트가 없으면 0). */
        uint32 computeCurrentLinkID() const;
        /** @brief 두 끝(센티미터로 자름)과 종류 이름으로 정한 링크 번호입니다. 0 이 아닙니다. */
        static uint32 computeLinkID( const float3& start, const float3& end, const hashed_string& linkType );

        const float3&        getStartOffset() const { return _startOffset; }
        void                 setStartOffset( const float3& offset );
        const float3&        getEndOffset() const { return _endOffset; }
        void                 setEndOffset( const float3& offset );
        float32              getWidth() const { return _width; }
        void                 setWidth( float32 width );
        bool                 isBidirectional() const { return _bBidirectional; }
        void                 setBidirectional( bool bBidirectional );
        const hashed_string& getAreaName() const { return _areaName; }
        void                 setAreaName( const hashed_string& areaName );
        const hashed_string& getLinkType() const { return _linkType; }
        void                 setLinkType( const hashed_string& linkType );
        bool                 isLinkEnabled() const { return _bLinkEnabled; }
        /** @brief 링크를 막거나 풉니다(닫힌 문). 다음 내비게이션 갱신에 적용됩니다. 아무 틱에서나 불러도 됩니다(게임 스레드). */
        void setLinkEnabled( bool bEnabled );

    private:
        /** @brief `_areaName` 가 내비 영역 표에 있는지 봅니다(`ComponentNameValidation`). */
        void validateAreaName( ValidationContext& context ) const;

    private:
        friend class SceneNavigation;

        /** @brief 지금 위치의 타일과 마지막으로 알린 위치의 타일을 다시 베이크하게 합니다. */
        void invalidateLinkArea();

        PROPERTY( Category = "Link", DisplayName = "Start", Tooltip = "Start point relative to the object", Units = m )
        float3 _startOffset;
        PROPERTY( Category = "Link", DisplayName = "End", Tooltip = "End point relative to the object", Units = m )
        float3 _endOffset;
        PROPERTY( Category = "Link", Min = 0.05, Tooltip = "How far each end may be from the navmesh and still connect", Units = m )
        float32 _width;
        PROPERTY( Category = "Link", DisplayName = "Area", Tooltip = "Navigation area of the link (empty = the object's area)", Validate = validateAreaName )
        hashed_string _areaName;
        PROPERTY( Category = "Link", DisplayName = "Link Type", Tooltip = "Name agents report while crossing (Ladder, Jump, Door) - the animation picks its move from it" )
        hashed_string _linkType;
        PROPERTY( Category = "Link", Tooltip = "Agents can also cross from the end to the start" )
        bool _bBidirectional;
        PROPERTY( Category = "Link", DisplayName = "Enabled", Tooltip = "A disabled link is not crossed (a closed door)" )
        bool _bLinkEnabled;

        AABB         _reportedBounds; ///< 마지막으로 다시 베이크하게 한 영역
        uint32       _navIndex;
        Subscription _navRegistration;   ///< `SceneNavigation` 등록 구독(`onRegister` 가 받고 `onUnregister` 가 놓는다)
        uint8        _bEnabledDirty : 1; ///< 막기 · 풀기를 아직 내비메시에 넣지 않았다
        uint8        _reserved      : 7;
    };
} // namespace sw
