/**
 * @file TickModeTestComponents.h
 * @brief 틱 모드 동등성 단위 테스트(`TestTickModes.cpp`)가 다는 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 순서에 민감한 상태(곱 · 덧셈을 섞은 값)를 주 틱 · 서브틱 · 틱 중 서브틱 등록 · 해제 · 남의 오브젝트 쓰기로 바꿔, 순서가 하나라도 틀리면
 *          값이 달라지게 한다. 일괄 경로(파도 · 모드)와 기준 길(오브젝트 칸)이 같은 값을 내는지 본다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include <thread>

namespace sw
{
    /**
     * @brief 주 틱(Normal) · 이른 서브틱(Early) · 늦은 서브틱(Late) · 틱 중에 등록 · 해제하는 뒤 그룹 서브틱을 가진 컴포넌트입니다.
     * @details 이른 → 주 → 늦은 순서가 지켜져야 `_value` 가 맞는다. 주 틱과 늦은 서브틱은 같은 남의 오브젝트(`_pShared`)에 잇따라 쓴다 — 늦은 쪽이 이겨야 한다.
     */
    REFLECT( HideInMenu )
    struct TickModeMoverTestComponent : public Component
    {
        REFLECT_BODY();

        static constexpr uint32 kEarlySubTick   = 1;
        static constexpr uint32 kLateSubTick    = 2;
        static constexpr uint32 kToggledSubTick = 3;

        float32         _value = 1.0f;
        float32         _speed = 1.0f;
        uint32          _frame = 0;
        SceneComponent* _pShared{ nullptr }; ///< 여러 무버가 쓰는 남의 오브젝트 루트(테스트가 넣는다)

        void onBeginPlay() override
        {
            Component::onBeginPlay();
            (void)registerSubTick( TickGroup::DuringPhysics, kEarlySubTick, TickPhase::Early );
            (void)registerSubTick( TickGroup::DuringPhysics, kLateSubTick, TickPhase::Late, 3 );
        }

        void onTick( TickContext& context ) override
        {
            _value += context.getDeltaTime() * _speed;
            writeOwnPosition( float3{ _value, 0.0f, 0.0f } );
            if ( _pShared != nullptr )
                _pShared->setLocalPosition( float3{ 0.0f, 0.0f, _value } );
            // 틱 중 등록 · 해제는 틱 뒤로 미뤄지고 다음 `refresh` 가 소속을 다시 정한다.
            if ( _frame % 3 == 0 )
                (void)registerSubTick( TickGroup::PostPhysics, kToggledSubTick );
            else if ( _frame % 3 == 1 )
                (void)unregisterSubTick( kToggledSubTick );
            ++_frame;
        }

        void onSubTick( uint32 subTickID, float32 deltaTime ) override
        {
            (void)deltaTime;
            if ( subTickID == kEarlySubTick )
            {
                _value = _value * 0.5f + 1.0f;
            }
            else if ( subTickID == kLateSubTick )
            {
                _value = _value * 1.25f;
                writeOwnPosition( float3{ _value, _value, 0.0f } );
                if ( _pShared != nullptr )
                    _pShared->setLocalPosition( float3{ 0.0f, 0.0f, -_value } );
            }
            else if ( subTickID == kToggledSubTick )
            {
                _value -= 0.25f;
            }
        }

        void writeOwnPosition( const float3& position )
        {
            SceneComponent* pRoot = getOwner()->getComponent<SceneComponent>();
            if ( pRoot != nullptr )
                pRoot->setLocalPosition( position );
        }
    };

    /** @brief 리더의 주 틱을 선행 조건으로 기다리는 서브틱 하나로, 같은 프레임에 리더가 옮긴 자리를 읽는 컴포넌트입니다(선행 조건 스테이지). */
    REFLECT( HideInMenu )
    struct TickModeFollowerTestComponent : public Component
    {
        REFLECT_BODY();

        static constexpr uint32 kFollowSubTick = 7;

        SceneComponent*             _pLeaderRoot{ nullptr };
        TickModeMoverTestComponent* _pLeader{ nullptr };
        float32                     _value           = 0.0f;
        uint32                      _followSubTickID = kFollowSubTick; ///< 이 팔로워의 서브틱 id(파생 타입이 바꿔 스테이지의 이웃 묶음을 가른다)

        void onBeginPlay() override
        {
            Component::onBeginPlay();
            (void)registerSubTick( TickGroup::DuringPhysics, _followSubTickID );
            if ( _pLeader != nullptr )
                (void)addSubTickPrerequisite( _followSubTickID, _pLeader->getTickHandle(), TickPrerequisiteKind::ReadsWrites ); // 리더가 옮긴 자리를 읽는다
        }

        void onSubTick( uint32 subTickID, float32 deltaTime ) override
        {
            (void)subTickID;
            (void)deltaTime;
            if ( _pLeaderRoot == nullptr )
                return;
            _value                = _pLeaderRoot->getLocalPosition()._x + 1.0f;
            SceneComponent* pRoot = getOwner()->getComponent<SceneComponent>();
            if ( pRoot != nullptr )
                pRoot->setLocalPosition( float3{ _value, 0.0f, 0.0f } );
        }
    };

    /** @brief 다른 타입 · 다른 서브틱 id 의 팔로워입니다 — 스테이지에서 팔로워와 섞여 들어, 자리 키가 같은 타입 · 서브틱의 이웃을 붙이는지 본다. */
    REFLECT( HideInMenu )
    struct TickModeFollowerAltTestComponent : public TickModeFollowerTestComponent
    {
        REFLECT_BODY();

        static constexpr uint32 kAltFollowSubTick = 8;

        TickModeFollowerAltTestComponent() { _followSubTickID = kAltFollowSubTick; }
    };

    /**
     * @brief 앞 고리의 값을 **멤버로 직접** 읽는 사슬 고리입니다(자리가 아니라서 순서만 선행 조건이면 된다 — 게임 모드 규칙 틱과 같은 모양).
     * @details 값은 그 프레임의 앞 고리 값만으로 정해진다 — 순서가 하나라도 틀리면(앞 고리보다 먼저 돌면) 지난 프레임 값을 읽어 기대값과 달라진다.
     */
    REFLECT( HideInMenu )
    struct TickModeChainTestComponent : public Component
    {
        REFLECT_BODY();

        static constexpr uint32 kChainSubTick = 9;

        TickModeChainTestComponent* _pPrev{ nullptr }; ///< 앞 고리(없으면 사슬의 머리 — 선행 조건 없이 파도에서 돈다)
        float32                     _value = 0.0f;
        float32                     _gain  = 1.0f;
        uint32                      _frame = 0;
        SubTickHandle               _chainHandle{};

        void onBeginPlay() override
        {
            Component::onBeginPlay();
            _chainHandle = registerSubTick( TickGroup::DuringPhysics, kChainSubTick );
        }

        void onSubTick( uint32 subTickID, float32 deltaTime ) override
        {
            (void)subTickID;
            ++_frame;
            const float32 previous = _pPrev != nullptr ? _pPrev->_value : static_cast<float32>( _frame );
            _value                 = previous * 0.5f + _gain + deltaTime;
        }
    };

    /** @brief Main 모드로 등록하는(테스트의 지역 줄) 컴포넌트입니다 — 틱한 스레드를 적는다. */
    REFLECT( HideInMenu )
    struct TickModeMainTestComponent : public Component
    {
        REFLECT_BODY();

        std::thread::id _tickThread{};
        uint32          _tickCount = 0;

        void onTick( TickContext& /*context*/ ) override
        {
            _tickThread = std::this_thread::get_id();
            ++_tickCount;
        }
    };

    /** @brief 등록 없이 Main 모드를 기반에서 물려받는 파생 컴포넌트입니다. */
    REFLECT( HideInMenu )
    struct TickModeMainDerivedTestComponent : public TickModeMainTestComponent
    {
        REFLECT_BODY();
    };
} // namespace sw
