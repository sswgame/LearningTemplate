/**
 * @file PhysicsHookRegistry.h
 * @brief 물리 프레임 훅(`beginPhysicsFrame` · `prePhysicsStep` · `postPhysicsStep` · `endPhysicsFrame`)의 타입별 일괄 함수 레지스트리입니다.
 *
 * [왜 필요한가]
 * 예전 `ScenePhysics` 는 모든 물리 컴포넌트를 훅마다(스텝 훅은 스텝마다) 가상 호출하고, 기본 구현이 불리면 런타임 비트를 세워 다음부터 뺐다.
 * 재정의 여부는 컴파일러가 이미 안다 — `&T::prePhysicsStep` 의 형식이 `PhysicsComponent` 의 멤버 포인터면
 * 그 타입은 훅을 물려받았다. 이 레지스트리는 타입마다 그 판정과 일괄 함수(같은 타입을 연속 배열로 한정 호출)를 둔다. 언리얼 `FTickFunction` 을
 * 재정의한 클래스만 등록하는 자리, Mass 프로세서가 같은 종류를 한 번에 도는 자리와 같다(5-3 "물리 훅 배치").
 *
 * [규약 — 리플렉션 생성기(5-4)와 맞춘 등록 API]
 * - 등록 한 줄은 `SW_REGISTER_PHYSICS_HOOKS( 타입 )` 입니다. 생성기가 `parsing.physics_hook_base_types`(`sw::PhysicsComponent`) 파생의 추상이 아닌
 *   타입마다 생성 `.cpp` 의 `namespace sw` 안에 냅니다. `PhysicsHooksOf<타입>` 이 훅을 한정 이름으로 부르므로 `REFLECT_BODY` 가 친구로 연다.
 * - 줄의 함수가 nullptr 이면 그 타입은 그 훅을 재정의하지 않았다(물려받은 기본 구현은 아무것도 하지 않는다) — 부를 것이 없다.
 * - 함수는 **정확한 타입만** 씁니다. 등록이 없는 타입(생성기를 거치지 않은 테스트 타입)은 지금처럼 가상 호출로 간다.
 * - 받는 쪽: `ScenePhysics::refreshFrameHookLists` 가 단계 목록에서 이어진 같은 타입을 구간으로 묶어 이 함수를 부른다(세대가 바뀌면 다시 짓는다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Module/ModuleEntryRegistrar.h"

namespace sw
{
    class PhysicsComponent;
    class ScenePhysics;
    struct TypeInfo;

    /** @brief 훅 인자 묶음입니다. 훅마다 쓰는 칸만 읽는다(스텝 훅은 고정 스텝 · 차례, 프레임 끝은 보간 비). */
    struct PhysicsHookArgs
    {
        float32 _fixedDeltaTime{ 0.0f };
        uint32  _stepIndex{ 0 };
        uint32  _stepCount{ 0 };
        float32 _alpha{ 0.0f };
    };

    /** @brief 같은 타입의 컴포넌트 @p count 개(@p ppComponent)에 훅 하나를 한정 호출하는 함수입니다. */
    using PhysicsHookBatchFunction = void ( * )( ScenePhysics& physics, PhysicsComponent* const* ppComponent, uint32 count, const PhysicsHookArgs& args );

    /** @brief 훅의 칸 차례입니다(`PhysicsHookRegistry::Entry::_arrFunction`). */
    enum class PhysicsHookSlot : uint8
    {
        BeginFrame,
        PreStep,
        PostStep,
        EndFrame,
        Count
    };

    /**
     * @class PhysicsHookRegistry
     * @brief 타입 → 물리 훅 일괄 함수의 레지스트리입니다(서비스 범위의 서브시스템). 파일 머리말 참고.
     * @note 조회(`findEntry`)는 게임 스레드에서 물리 프레임 밖에만 합니다. 등록 · 해제는 모듈을 올리고 내리는 스레드에서 옵니다.
     */
    class SW_API PhysicsHookRegistry final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "PhysicsHookRegistry";

        /** @brief 타입의 `StaticType` 입니다. 주소가 모듈 재적재로 바뀔 수 있어 조회할 때마다 부릅니다. */
        using StaticTypeFunction = const TypeInfo* (*)();

        /**
         * @struct Entry
         * @brief 등록 줄 하나입니다. 등록자(`PhysicsHookRegistrar`)가 멤버로 들고, 레지스트리는 그 주소를 등록 목록에 올립니다.
         */
        struct Entry
        {
            StaticTypeFunction       _pStaticType;
            PhysicsHookBatchFunction _arrFunction[static_cast<size_t>( PhysicsHookSlot::Count )]; ///< 재정의하지 않은 훅은 nullptr
        };

        // ISubsystem — 세울 것이 없다(줄은 모듈을 올리는 길이 붙인다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 줄 하나를 등록합니다. 세대를 올립니다. 타입이 비었으면 false 입니다. */
        [[nodiscard]] bool registerEntry( Entry& entry ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 자기 줄의 등록을 지웁니다(모듈 해제). 오르지 않은 줄이면 아무것도 하지 않습니다. 세대를 올립니다. */
        void unregisterEntry( Entry& entry ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 정확히 @p pType 의 줄입니다. 나중에 오른 줄이 먼저다. 없으면 nullptr(가상 호출 경로)입니다. */
        const Entry* findEntry( const TypeInfo* pType ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 등록 · 해제마다 오르는 세대입니다. 훅 목록을 지은 쪽이 바뀐 것을 보고 다시 짓습니다. */
        uint32 getGeneration() const { return _generation.load( std::memory_order_acquire ); }

    private:
        mutex                              _mutex;
        RegistrationList<Entry> _listEntry SW_GUARDED_BY( _mutex ){ "PhysicsHookRegistry" };
        atomic<uint32>                     _generation{ 0 };

        SW_SERVICE_SLOT( PhysicsHookRegistry, Required );
    };

    /** @brief 물리 훅 줄 하나의 정적 등록자입니다. 링크 때 목록에 달리는 객체라 정적 객체로만 둔다. */
    using PhysicsHookRegistrar = ModuleEntryRegistrar<PhysicsHookRegistry, PhysicsHookRegistry::Entry>;

    /**
     * @brief 타입 @p TComponent 의 물리 훅 일괄 함수와 등록 줄입니다. `SW_REGISTER_PHYSICS_HOOKS` 가 그 타입의 등록 TU 에서 인스턴스화합니다.
     * @details 재정의 여부는 `&TComponent::훅` 의 클래스 형식으로 컴파일에서 정한다 — `PhysicsComponent` 의 멤버 포인터면 물려받은 기본 구현이다.
     *          중간 기반이 재정의했으면 그 기반의 형식이라 "재정의함" 이고, 한정 호출은 그 기반의 구현을 부른다(가상 호출과 같은 함수).
     */
    template <typename TComponent>
    struct PhysicsHooksOf
    {
        static constexpr bool kOverridesBeginFrame = std::is_same_v<decltype( &TComponent::beginPhysicsFrame ), void ( PhysicsComponent::* )( ScenePhysics& )> == false;
        static constexpr bool kOverridesPreStep =
            std::is_same_v<decltype( &TComponent::prePhysicsStep ), void ( PhysicsComponent::* )( ScenePhysics&, float32, uint32, uint32 )> == false;
        static constexpr bool kOverridesPostStep = std::is_same_v<decltype( &TComponent::postPhysicsStep ), void ( PhysicsComponent::* )( ScenePhysics& )> == false;
        static constexpr bool kOverridesEndFrame = std::is_same_v<decltype( &TComponent::endPhysicsFrame ), void ( PhysicsComponent::* )( ScenePhysics&, float32 )> == false;

        static void beginFrame( ScenePhysics& physics, PhysicsComponent* const* ppComponent, const uint32 count, const PhysicsHookArgs& )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                static_cast<TComponent*>( ppComponent[index] )->TComponent::beginPhysicsFrame( physics );
            }
        }
        static void preStep( ScenePhysics& physics, PhysicsComponent* const* ppComponent, const uint32 count, const PhysicsHookArgs& args )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                static_cast<TComponent*>( ppComponent[index] )->TComponent::prePhysicsStep( physics, args._fixedDeltaTime, args._stepIndex, args._stepCount );
            }
        }
        static void postStep( ScenePhysics& physics, PhysicsComponent* const* ppComponent, const uint32 count, const PhysicsHookArgs& )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                static_cast<TComponent*>( ppComponent[index] )->TComponent::postPhysicsStep( physics );
            }
        }
        static void endFrame( ScenePhysics& physics, PhysicsComponent* const* ppComponent, const uint32 count, const PhysicsHookArgs& args )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                static_cast<TComponent*>( ppComponent[index] )->TComponent::endPhysicsFrame( physics, args._alpha );
            }
        }

        /** @brief 등록 줄입니다. 재정의하지 않은 훅의 칸은 nullptr 입니다. */
        static PhysicsHookRegistry::Entry makeEntry()
        {
            return PhysicsHookRegistry::Entry{
                &TComponent::StaticType,
                { kOverridesBeginFrame ? &beginFrame : nullptr, kOverridesPreStep ? &preStep : nullptr,
                                        kOverridesPostStep ? &postStep : nullptr, kOverridesEndFrame ? &endFrame : nullptr }
            };
        }
    };
} // namespace sw

/**
 * @brief 물리 컴포넌트 타입의 훅 일괄 함수를 올립니다 — 리플렉션 생성기가 부르는 등록 줄입니다(`SW_REGISTER_TICK_TYPE` 과 같은 모양).
 * @details 그 타입의 생성 `.cpp` 의 `namespace sw` 안에 `SW_REGISTER_PHYSICS_HOOKS( 타입 );` 로 한 번 씁니다. 다른 이름공간의 타입은 생성기가 같은 줄을 펼쳐 쓴다.
 */
#define SW_REGISTER_PHYSICS_HOOKS( Type )                             \
    static ::sw::PhysicsHookRegistrar s_physicsHookRegistration##Type \
    {                                                                 \
        ::sw::PhysicsHooksOf<Type>::makeEntry()                       \
    }
