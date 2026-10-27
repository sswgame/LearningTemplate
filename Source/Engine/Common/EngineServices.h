/**
 * @file EngineServices.h
 * @brief 엔진 범위의 서비스 창구입니다 — `engine::getSubsystem<T>()`(반드시 있음) · `engine::findSubsystem<T>()`(없을 수 있음).
 *
 * @details 서비스 접근은 **소유 수명마다 한 창구, 한 어휘**입니다. `get` 은 `T&` 를 돌려주고 비었으면 멈추며, `find` 는 `T*` 를 돌려준다.
 *          | 수명 | 창구 |
 *          | --- | --- |
 *          | 엔진(프로세스) | `engine::getSubsystem<T>()` · `engine::findSubsystem<T>()` — 이 헤더 |
 *          | 씬 | `GameObjectManager::getSubsystem<T>()` |
 *          | 게임 인스턴스 | `game::findSubsystem<T>()`(게임 범위 → 호스트), 인스턴스 멤버 `getSubsystem<T>()` |
 *          | 에디터 | `editor::findSubsystem<T>()`(에디터 범위 → 호스트), 컨텍스트 멤버 `getSubsystem<T>()` · `findSubsystem<T>()` |
 *          언리얼 `GEngine->GetEngineSubsystem<T>()` · `UWorld::GetSubsystem<T>()` 와 같은 자리입니다. 언리얼은 부를 때마다 맵을 찾는 것으로 알고 있고,
 *          여기서는 `EngineServiceList.xxx` 의 행을 바인딩 때 한 번 찾아 칸에 두고 템플릿이 칸 색인을 컴파일 때 고른다 — 캐스트마다 타입 레지스트리를
 *          묻는 자리가 이름 조회를 하지 않는다. 표에 없는 타입은 `findSubsystem<T>()` 가 범위를 올라가며 찾고, `getSubsystem<T>()` 는 컴파일 오류다.
 *          **서비스를 더하려면 서브시스템으로 만들어 호스트의 범위에 넣고, 자주 찾으면 목록에 한 줄을 더합니다.** 소유는 범위(`SubsystemScope`)이고
 *          칸은 아무것도 소유하지 않습니다. 테스트는 `test::ScopedEngineServiceOverride` 로 범위에 목을 넣어 다시 묶는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Common/EngineScope.h"

#include <type_traits>

#if !defined( SW_ENGINE_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL ) && !defined( SW_TOOL_INTERNAL )
    #error "EngineServices.h can only be included internally by the Engine, App, or Tests."
#endif

namespace sw
{
#define SW_ENGINE_SERVICE( Tag, Type, requirement, visibility )       Tag Type;
#define SW_ENGINE_SERVICE_CONST( Tag, Type, requirement, visibility ) Tag Type;
#define SW_ENGINE_SERVICE_OPT( Tag, Type, visibility )                Tag Type;
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

    // ------------------------------------------------------------------------------
    // 1) 칸 색인과 타입 → 칸 trait(목록에서 생성)
    // ------------------------------------------------------------------------------
    /** @brief 목록 행의 칸 색인입니다. 열거자 이름이 그 행의 타입 이름입니다. */
    enum class EngineServiceIndex : uint32
    {
#define SW_ENGINE_SERVICE( Tag, Type, requirement, visibility )       Type,
#define SW_ENGINE_SERVICE_CONST( Tag, Type, requirement, visibility ) Type,
#define SW_ENGINE_SERVICE_OPT( Tag, Type, visibility )                Type,
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
        Count
    };

    /** @brief 타입 @p T 의 목록 행입니다. 목록에 없으면 `kListed` 가 false 입니다. */
    template <class T>
    struct EngineServiceSlot
    {
        static constexpr bool kListed = false;
        using Access                  = T;
    };

// 인자가 **타입 이름**이라 괄호를 씌울 수 없다(`template <> struct EngineServiceSlot<(Type)>` 는 올바른 문법이 아니다).
// NOLINTBEGIN(bugprone-macro-parentheses)
#define SW_ENGINE_SERVICE_SLOT( Type, AccessType, bNullable )                                     \
    template <>                                                                                   \
    struct EngineServiceSlot<Type>                                                                \
    {                                                                                             \
        static constexpr bool        kListed   = true;                                            \
        static constexpr bool        kNullable = bNullable;                                       \
        static constexpr uint32      kIndex    = static_cast<uint32>( EngineServiceIndex::Type ); \
        static constexpr const utf8* kTypeName = #Type;                                           \
        using Access                           = AccessType;                                      \
    };
#define SW_ENGINE_SERVICE( Tag, Type, requirement, visibility )       SW_ENGINE_SERVICE_SLOT( Type, Type, false )
#define SW_ENGINE_SERVICE_CONST( Tag, Type, requirement, visibility ) SW_ENGINE_SERVICE_SLOT( Type, const Type, false )
#define SW_ENGINE_SERVICE_OPT( Tag, Type, visibility )                SW_ENGINE_SERVICE_SLOT( Type, Type, true )
// NOLINTEND(bugprone-macro-parentheses)
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
#undef SW_ENGINE_SERVICE_SLOT

    namespace engine
    {
        // ------------------------------------------------------------------------------
        // 2) 바인딩 — 호스트가 범위를 세운 뒤 묶고, 그 범위를 내리기 전에 끊는다
        // ------------------------------------------------------------------------------
        /**
         * @brief @p subsystems(엔진 범위 — 부모 범위까지 찾는다)에서 목록의 행을 모두 찾아 둡니다.
         * @return 비어 있는 **첫 필수 서비스의 타입 이름**입니다. 모두 찾았으면 nullptr 입니다. 호스트는 이것이 nullptr 가 아니면 서지 않는다 —
         *         그 함수로 게이팅되는 스무 곳이 조용히 폴백으로 가는 것(배포본에서 셰이더 캐시를 건너뛰고 DXC 를 부르다 죽은 적이 있다)을 기동에서 막는다.
         * @note 로케이터는 끊을 때까지 살아 있어야 합니다(`getEngineSubsystems` 가 돌려준다).
         */
        SW_API const utf8* bindEngineServices( const ISubsystemLocator& subsystems );
        /** @brief 바인딩을 끊습니다. 찾아 둔 서비스가 소멸하기 전에 부릅니다. */
        SW_API void unbindEngineServices();
        /**
         * @brief `Required` 인 서비스를 모두 찾았는지 반환합니다. bind/unbind 때 한 번 센 플래그입니다.
         * @details 선택(`Optional`) 항목의 원본은 `EngineServiceList.xxx` 입니다. 목록이 바뀌면 이 설명이 아니라 목록을 믿으십시오.
         */
        SW_API bool areEngineServicesBound();
        /** @brief @p subsystems 에서 찾을 수 없는 첫 필수 서비스의 타입 이름입니다. 바인딩을 흔들지 않고 물어볼 수 있다. 모두 있으면 nullptr 입니다. */
        SW_API const utf8* findUnboundRequiredServiceName( const ISubsystemLocator& subsystems );
        /** @brief 지금 찾아 둔 칸에서 비어 있는 첫 필수 서비스의 타입 이름입니다. 모두 있으면 nullptr 입니다. */
        SW_API const utf8* findUnboundRequiredServiceName();
        /**
         * @brief 모듈(게임 · 에디터)에 넘길 범위 @p outScope 에 지금 찾아 둔 서비스를 그 타입으로 겁니다(빌려 건다 — 서비스가 범위보다 오래 산다).
         * @param bGameModuleOnly true 면 `GameVisible` 행만 겁니다 — 게임 모듈은 호스트 전용 서비스를 찾지 못한다.
         * @param bSkipAlreadyBound true 면 @p outScope 에 이미 걸린 타입은 건너뜁니다 — 테스트가 자기 서비스(예: 씬 매니저)를 먼저 걸고 나머지를 채울 때만 쓴다. false 면 겹침은 결함이라 멈춘다.
         * @note 빈 행은 걸지 않는다. 호스트 전용 서비스(모듈 컴파일러)는 호스트가 그 뒤에 더 건다.
         */
        SW_API void bindModuleServices( SubsystemScope& outScope, bool bGameModuleOnly, bool bSkipAlreadyBound = false );

        namespace internal
        {
            /** @brief 찾아 둔 칸입니다(`EngineServiceIndex` 순서). 바인딩 전이면 모두 nullptr 입니다. 창구 템플릿만 읽는다. */
            SW_API void* const* getServiceSlots();
        } // namespace internal

        // ------------------------------------------------------------------------------
        // 3) 서비스 창구
        // ------------------------------------------------------------------------------
        /**
         * @brief 엔진 서비스 @p T 입니다. 바인딩 뒤에는 늘 있다 — 비었으면 부른 쪽의 잘못이라 멈춥니다.
         * @details 목록(`EngineServiceList.xxx`)의 행만 됩니다. 없을 수 있는 행(`SW_ENGINE_SERVICE_OPT`)과 목록 밖 타입은 컴파일 오류이고 `findSubsystem<T>()` 를 씁니다.
         */
        template <class T>
        typename EngineServiceSlot<std::remove_const_t<T>>::Access& getSubsystem()
        {
            using Slot = EngineServiceSlot<std::remove_const_t<T>>;
            static_assert( Slot::kListed, "engine::getSubsystem<T>: T is not in EngineServiceList.xxx - add a row, or use engine::findSubsystem<T>()" );
            static_assert( Slot::kNullable == false, "engine::getSubsystem<T>: this row may be empty - use engine::findSubsystem<T>()" );
            void* const pService = internal::getServiceSlots()[Slot::kIndex];
            if ( pService == nullptr )
                ::sw::internal::reportMissingRequiredService( Slot::kTypeName ); // 모든 구성에서 멈춘다 — `SW_LOG_ASSERT` 는 Release 에서 로그만 남기고 널을 역참조했다
            return *static_cast<typename Slot::Access*>( pService );
        }

        /**
         * @brief 엔진 서비스 @p T 입니다. **없으면 nullptr 이므로** 받는 쪽이 확인해야 합니다(바인딩 전 · 선택 행 · 하네스가 넣지 않은 서비스).
         * @details 목록의 행은 찾아 둔 칸을 읽고, 목록 밖 타입은 바인딩한 범위에서 상위로 올라가며 찾는다(선형 검색 — 핫 패스면 목록에 넣는다).
         */
        template <class T>
        typename EngineServiceSlot<std::remove_const_t<T>>::Access* findSubsystem()
        {
            using Slot = EngineServiceSlot<std::remove_const_t<T>>;
            if constexpr ( Slot::kListed )
            {
                return static_cast<typename Slot::Access*>( internal::getServiceSlots()[Slot::kIndex] );
            }
            else
            {
                const ISubsystemLocator* pEngineScope = getEngineSubsystems(); // 범위를 직접 올라가는 것은 이 창구뿐이다
                return locateSubsystem<T>( pEngineScope );
            }
        }
    } // namespace engine
} // namespace sw
