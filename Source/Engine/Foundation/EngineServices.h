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
 *          언리얼 `GEngine->GetEngineSubsystem<T>()` · `UWorld::GetSubsystem<T>()` 와 같은 자리입니다. 언리얼은 부를 때마다 맵을 찾는 것으로 알고 있고(기억),
 *          여기서는 타입마다 가진 칸(`ServiceSlotOf<T>::s_slot`, `Core/Subsystem/ServiceSlot.h`)을 바인딩 때 한 번 채우고 창구는 그 칸만 읽는다 —
 *          캐스트마다 타입 레지스트리를 묻는 자리가 이름 조회를 하지 않는다. 중앙 목록은 없다: 칸은 타입 헤더가 선언하고, 그 객체를 범위에 넣는 .cpp 의
 *          등록 줄(`Engine/Foundation/EngineSubsystemRegistration.h`)이 정의한다. 칸을 선언하지 않은 타입은 `findSubsystem<T>()` 가 범위를 올라가며 찾고
 *          (선형 검색 — 핫 패스면 칸을 선언한다), `getSubsystem<T>()` 는 컴파일 오류다. 소유는 범위(`SubsystemScope`)이고 칸은 아무것도 소유하지 않습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Foundation/EngineScope.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

#include <type_traits>

#if !defined( SW_ENGINE_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL ) && !defined( SW_TOOL_INTERNAL )
    #error "EngineServices.h can only be included internally by the Engine, App, or Tests."
#endif

namespace sw
{
    namespace engine
    {
        // ------------------------------------------------------------------------------
        // 1) 바인딩 — 호스트가 범위를 세운 뒤 묶고, 그 범위를 내리기 전에 끊는다
        // ------------------------------------------------------------------------------
        /**
         * @brief @p subsystems(엔진 범위 — 부모 범위까지 찾는다)에서 등록된 칸을 모두 찾아 둡니다.
         * @return `Bound` 가 아니면 호스트는 서지 않는다. `MissingRequired` 는 비어 있는 **필수 칸의 타입 이름**을 준다 — 그 함수로 게이팅되는 스무 곳이 조용히
         *         폴백으로 가는 것(배포본에서 셰이더 캐시를 건너뛰고 DXC 를 부르다 죽은 적이 있다)을 기동에서 막는다. `AlreadyBound` 는 이 프로세스에 이미 엔진이
         *         묶였다는 뜻이다 — **프로세스당 엔진 하나**가 계약이라(칸은 타입마다 하나) 앞 엔진의 칸을 덮지 않고 거절한다.
         * @note 로케이터는 끊을 때까지 살아 있어야 합니다(`getEngineSubsystems` 가 돌려준다).
         */
        [[nodiscard]] SW_API EngineServiceBindResult bindEngineServices( const ISubsystemLocator& subsystems );
        /** @brief @p subsystems 로 묶은 바인딩을 끊습니다. 찾아 둔 서비스가 소멸하기 전에 부릅니다. 다른 범위(묶지 못한 엔진)면 아무것도 하지 않는다. */
        SW_API void unbindEngineServices( const ISubsystemLocator& subsystems );
        /**
         * @brief 필수 칸(`SW_SERVICE_SLOT( T, Required )`)을 모두 찾았는지 반환합니다. bind/unbind 때 한 번 센 플래그입니다.
         * @details `Component::getTypeInfo()` 가 캐스트마다 이것을 부르므로 부를 때마다 칸을 훑지 않는다.
         */
        SW_API bool areEngineServicesBound();
        /**
         * @brief 모듈(게임 · 에디터)에 넘길 범위 @p outScope 에 지금 찾아 둔 서비스를 그 타입으로 겁니다(빌려 건다 — 서비스가 범위보다 오래 산다).
         * @param bGameModuleOnly true 면 `GameVisible` 로 등록한 칸만 겁니다 — 게임 모듈은 호스트 전용 서비스를 찾지 못한다.
         * @param bSkipAlreadyBound true 면 @p outScope 에 이미 걸린 타입은 건너뜁니다 — 테스트가 자기 서비스(예: 씬 매니저)를 먼저 걸고 나머지를 채울 때만 쓴다. false 면 겹침은 결함이라 멈춘다.
         * @note 빈 칸은 걸지 않는다. 호스트 전용 서비스(모듈 컴파일러)는 호스트가 그 뒤에 더 건다.
         */
        SW_API void bindModuleServices( SubsystemScope& outScope, bool bGameModuleOnly, bool bSkipAlreadyBound = false );

        // ------------------------------------------------------------------------------
        // 2) 서비스 창구
        // ------------------------------------------------------------------------------
        /**
         * @brief 엔진 서비스 @p T 입니다. 바인딩 뒤에는 늘 있다 — 비었으면 부른 쪽의 잘못이라 멈춥니다.
         * @details 필수 칸(`SW_SERVICE_SLOT( T, Required )`)만 됩니다. 비어 있을 수 있는 칸(`Optional`)과 칸이 없는 타입은 컴파일 오류이고 `findSubsystem<T>()` 를 씁니다.
         *          칸 선언은 타입 헤더에 있으므로 그 헤더를 include 해야 한다(앞으로 선언만으로는 칸이 보이지 않는다).
         */
        template <class T>
        typename ServiceSlotOf<std::remove_const_t<T>>::Access& getSubsystem()
        {
            using Slot = ServiceSlotOf<std::remove_const_t<T>>;
            static_assert( Slot::kRegistered, "engine::getSubsystem<T>: T has no service slot - declare SW_SERVICE_SLOT in its header (and include it), register it with SW_ENGINE_SERVICE*, or use engine::findSubsystem<T>()" );
            if constexpr ( Slot::kRegistered )
            {
                static_assert( Slot::kRequirement == ServiceRequirement::Required, "engine::getSubsystem<T>: this slot may be empty (Optional) - use engine::findSubsystem<T>()" );
                void* const pService = Slot::s_slot._pBound;
                if ( pService == nullptr )
                    ::sw::internal::reportMissingRequiredService( Slot::kTypeName ); // 모든 구성에서 멈춘다 — `SW_LOG_ASSERT` 는 Release 에서 로그만 남기고 널을 역참조했다
                return *static_cast<typename Slot::Access*>( pService );
            }
        }

        /**
         * @brief 엔진 서비스 @p T 입니다. **없으면 nullptr 이므로** 받는 쪽이 확인해야 합니다(바인딩 전 · 선택 칸 · 하네스가 넣지 않은 서비스).
         * @details 칸이 있는 타입은 그 칸을 읽고, 칸이 없는 타입은 바인딩한 범위에서 상위로 올라가며 찾는다(선형 검색 — 핫 패스면 칸을 선언한다).
         *          `const T` 로 찾으면 const 로 건 객체(전용 서버의 `const ServerConfig`)도 찾고 const 포인터를 돌려준다(`findInLocator` 와 같은 규칙).
         */
        template <class T>
        std::conditional_t<std::is_const_v<T>, const typename ServiceSlotOf<std::remove_const_t<T>>::Access, typename ServiceSlotOf<std::remove_const_t<T>>::Access>* findSubsystem()
        {
            using Slot = ServiceSlotOf<std::remove_const_t<T>>;
            if constexpr ( Slot::kRegistered )
            {
                return static_cast<typename Slot::Access*>( Slot::s_slot._pBound );
            }
            else
            {
                const ISubsystemLocator* pEngineScope = getEngineSubsystems(); // 범위를 직접 올라가는 것은 이 창구뿐이다
                return locateSubsystem<T>( pEngineScope );
            }
        }
    } // namespace engine
} // namespace sw
