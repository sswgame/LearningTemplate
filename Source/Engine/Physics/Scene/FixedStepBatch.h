/**
 * @file FixedStepBatch.h
 * @brief 물리 컴포넌트 타입의 고정 스텝 훅 일괄 함수(`FixedStepBatchOf<T>`)와 등록 줄(`SW_REGISTER_FIXED_STEP_TYPE`)입니다.
 *
 * [규약 — 리플렉션 생성기와 맞춘 등록 API]
 * - 등록 한 줄은 `SW_REGISTER_FIXED_STEP_TYPE( 타입 )` 입니다. 생성기가 `parsing.fixed_step_base_types`(`sw::PhysicsComponent`) 파생의 추상이 아닌 타입마다
 *   생성 `.cpp` 의 `namespace sw` 안에 냅니다. `FixedStepBatchOf<타입>` 이 훅을 한정 이름으로 부르고 `&타입::훅` 을 보므로 `REFLECT_BODY` 가 친구로 연다.
 * - 훅 넷(`onPhysicsFrameBegin` · `onPrePhysicsStep` · `onPostPhysicsStep` · `onPhysicsFrameEnd`)은 **가상 함수가 아니다.** `PhysicsComponent` 가 정의 없는
 *   표식으로 선언하고, 타입이 같은 서명으로 다시 선언하면 재정의다 — `&T::훅` 의 클래스 형식이 `PhysicsComponent` 면 물려받은 표식이라 그 그룹 칸은 nullptr 이다.
 *   중간 기반이 재정의했으면 그 기반의 형식이라 "재정의함" 이고, 한정 호출은 그 기반의 구현을 부른다.
 * - 함수는 **정확한 타입만** 씁니다(`TickBatchRegistry::findFixedStepEntry`). 등록이 없는 타입의 훅은 부르지 않는다 — 가상 호출 길이 없다.
 * - 받는 쪽: 틱 스케줄러의 고정 스텝 목록(`FixedStepTickList`)이 차례 그대로 이어진 같은 함수를 구간으로 묶어 부른다.
 */
#pragma once
#include "Core/Container/EnumArray.h"

#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"
#include "Engine/Physics/Component/PhysicsComponent.h"

namespace sw
{
    /**
     * @brief 타입 @p TComponent 의 고정 스텝 훅 일괄 함수와 등록 줄입니다. `SW_REGISTER_FIXED_STEP_TYPE` 이 그 타입의 등록 TU 에서 인스턴스화합니다(훅이 인라인될 수 있게).
     * @details 재정의 여부는 `&TComponent::훅` 의 클래스 형식으로 컴파일에서 정한다(파일 머리말). `TickBatchOf` 와 같은 자리다.
     */
    template <typename TComponent>
    struct FixedStepBatchOf
    {
        /** @brief 표식(`PhysicsComponent` 의 정의 없는 선언)의 형식입니다 — 이 형식이면 그 그룹을 재정의하지 않았다. */
        using MarkerFunction = void ( PhysicsComponent::* )( const FixedStepContext& );

        static constexpr bool kOverridesFrameBegin = std::is_same_v<decltype( &TComponent::onPhysicsFrameBegin ), MarkerFunction> == false;
        static constexpr bool kOverridesPreStep    = std::is_same_v<decltype( &TComponent::onPrePhysicsStep ), MarkerFunction> == false;
        static constexpr bool kOverridesPostStep   = std::is_same_v<decltype( &TComponent::onPostPhysicsStep ), MarkerFunction> == false;
        static constexpr bool kOverridesFrameEnd   = std::is_same_v<decltype( &TComponent::onPhysicsFrameEnd ), MarkerFunction> == false;

        static void frameBegin( const FixedStepContext& context, Component* const* ppComponent, const uint32 count )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                static_cast<TComponent*>( ppComponent[index] )->TComponent::onPhysicsFrameBegin( context );
            }
        }
        static void preStep( const FixedStepContext& context, Component* const* ppComponent, const uint32 count )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                static_cast<TComponent*>( ppComponent[index] )->TComponent::onPrePhysicsStep( context );
            }
        }
        static void postStep( const FixedStepContext& context, Component* const* ppComponent, const uint32 count )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                static_cast<TComponent*>( ppComponent[index] )->TComponent::onPostPhysicsStep( context );
            }
        }
        static void frameEnd( const FixedStepContext& context, Component* const* ppComponent, const uint32 count )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                static_cast<TComponent*>( ppComponent[index] )->TComponent::onPhysicsFrameEnd( context );
            }
        }

        /** @brief 등록 줄입니다. 재정의하지 않은 그룹의 칸은 nullptr 입니다. */
        static TickBatchRegistry::FixedStepEntry makeEntry()
        {
            // 그룹을 더하고 여기를 잊으면 makeEnumTable 이 컴파일을 멈춘다(중괄호 목록은 남은 칸을 nullptr 로 채웠다).
            return TickBatchRegistry::FixedStepEntry{ &TComponent::StaticType, makeEnumTable<FixedStepGroup, FixedStepBatchFunction>(
                                                                                   kOverridesFrameBegin ? &frameBegin : nullptr, kOverridesPreStep ? &preStep : nullptr,
                                                                                   kOverridesPostStep ? &postStep : nullptr, kOverridesFrameEnd ? &frameEnd : nullptr ) };
        }
    };
} // namespace sw

/**
 * @brief 물리 컴포넌트 타입의 고정 스텝 훅 일괄 함수를 올립니다 — 리플렉션 생성기가 부르는 등록 줄입니다(`SW_REGISTER_TICK_TYPE` 과 같은 모양).
 * @details 그 타입의 생성 `.cpp` 의 `namespace sw` 안에 `SW_REGISTER_FIXED_STEP_TYPE( 타입 );` 로 한 번 씁니다. 다른 이름공간의 타입은 생성기가 같은 줄을 펼쳐 쓴다.
 */
#define SW_REGISTER_FIXED_STEP_TYPE( Type )                       \
    static ::sw::FixedStepRegistrar s_fixedStepRegistration##Type \
    {                                                             \
        ::sw::FixedStepBatchOf<Type>::makeEntry()                 \
    }
