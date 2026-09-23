/**
 * @file Engine/EngineOwnedServices.h
 * @brief 호스트가 소유하는 엔진 서비스들의 **저장소**입니다. `EngineServiceList.xxx` 에서 생성됩니다.
 *
 * [왜 필요한가]
 * 서비스 목록은 하나인데 **그것을 만들고 표에 연결하는 코드는 호스트마다 한 벌**이었습니다
 * (`EngineLoop::initialize` 와 `Test/TestFramework/main.cpp` 에 각각 스무 줄 넘게). 목록에 한 줄을
 * 더하면 멤버 선언 · `make_unique` · 대입을 **두 곳에** 손으로 적어야 했고, 빠뜨리면
 * `areEngineServicesBound()` 가 영영 false 가 되어 그것으로 게이팅되는 스무 곳이 조용히 폴백으로
 * 갔습니다(배포본에서 셰이더 캐시를 건너뛰고 DXC 를 부르다 죽은 사고가 목록 주석에 남아 있습니다).
 *
 * 이제 그 세 가지가 목록의 `owned=1` 한 글자에서 생성됩니다. 호스트는 이 구조체를 하나 들고
 * `createAll()` · `bindInto()` 만 부릅니다.
 *
 * [왜 `Common/` 이 아니라 루트인가]
 * 이 저장소는 서비스 스무 개의 **완전한 타입**을 압니다. `Engine/Common` 은 티어 0(엔진의 아무것도
 * 참조하지 않는 토대)이라 거기 두면 레이어 규칙을 어깁니다. `CheckEngineLayers` 가 12건으로 잡아냈습니다.
 * 루트(`Source/Engine` 바로 아래)는 티어 6, **"모두를 엮는 자리"** 이고 `EngineLoop` 가 있는 곳입니다.
 * 예외 목록에 이름을 적는 대신 **맞는 자리로 옮겼습니다.**
 *
 * [일부러 하지 않는 것]
 * **초기화 순서와 종료 순서는 생성하지 않습니다.** 이 저장소는 "누가 만들고 누가 들고 있는가" 만
 * 압니다. 무엇을 언제 `initialize()` 하고 어떤 순서로 내리는지는 호스트가 **손으로 적습니다.**
 * 그 순서는 이 저장소가 알 수 없는 사실(디바이스가 사라지기 전에 무엇을 놓아야 하는지, 씬이 사라진
 * 뒤에 모듈을 내려야 한다는 것)로 정해지고, 그 줄마다 과거에 한 번씩 무너진 이유가 주석으로
 * 붙어 있습니다. 순서를 자동으로 정하려면 그 지식을 의존성으로 다시 적어야 하는데, 그것은 같은 것을
 * 두 번 적는 일입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"

#if !defined( SW_ENGINE_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL ) && !defined( SW_TOOL_INTERNAL )
    #error "EngineOwnedServices.h can only be included internally by the Engine, App, or Tests."
#endif

// 인자가 **타입 이름과 선언자 이름**이라 괄호를 씌울 수 없다(EngineServices.h 와 같은 이유).
// NOLINTBEGIN(bugprone-macro-parentheses)
// owned 열에 따라 갈라지는 자리들이다. `SW_CONCAT` 으로 0/1 을 붙여 고른다. `if constexpr` 로는
// **선언**을 지울 수 없어서(멤버 선언과 헤더 포함이 걸린다) 이 방식이 필요하다.
#define SW_ENGINE_OWNED_STORAGE_0( member, Type )
#define SW_ENGINE_OWNED_STORAGE_1( member, Type ) unique_ptr<Type> member{};
#define SW_ENGINE_OWNED_CREATE_0( member, Type )
#define SW_ENGINE_OWNED_CREATE_1( member, Type ) \
    if ( member == nullptr )                     \
    {                                            \
        member = make_unique<Type>();            \
    }
#define SW_ENGINE_OWNED_BIND_0( member )
#define SW_ENGINE_OWNED_BIND_1( member ) outServices.member = member.get();
#define SW_ENGINE_OWNED_DESTROY_0( member )
#define SW_ENGINE_OWNED_DESTROY_1( member ) member.reset();
// NOLINTEND(bugprone-macro-parentheses)

namespace sw
{
    /**
     * @struct EngineOwnedServices
     * @brief `owned=1` 인 서비스의 소유권을 들고 있습니다. 호스트가 멤버로 하나 둡니다.
     * @note 소멸 순서는 **선언의 역순**, 즉 목록의 역순입니다. 그래도 호스트는 자기 종료 절차에서
     *       필요한 것을 먼저 명시적으로 놓습니다. 순서가 중요한 것들은 거기서 정해집니다.
     */
    struct SW_API EngineOwnedServices
    {
        /** @brief 빈 저장소를 만듭니다. 서비스를 만드는 것은 `createAll()` 입니다. */
        EngineOwnedServices();
        /**
         * @brief 남은 소유를 놓습니다.
         * @details **정의는 `.cpp` 에 있습니다.** 여기서 인라인으로 두면 이 헤더를 include 하는 모든 TU 가
         *          서비스 스무 개의 완전한 타입을 알아야 합니다(unique_ptr 의 소멸자가 그렇습니다).
         *          실제로 그렇게 두었다가 엔진 곳곳이 컴파일되지 않았습니다.
         */
        ~EngineOwnedServices();

        EngineOwnedServices( const EngineOwnedServices& )            = delete;
        EngineOwnedServices& operator=( const EngineOwnedServices& ) = delete;
// 인자가 **타입 이름과 선언자 이름**이라 괄호를 씌울 수 없다(EngineServices.h 와 같은 이유).
// NOLINTBEGIN(bugprone-macro-parentheses)
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )       SW_CONCAT( SW_ENGINE_OWNED_STORAGE_, owned )( member, Type )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) SW_CONCAT( SW_ENGINE_OWNED_STORAGE_, owned )( member, Type )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned )             SW_CONCAT( SW_ENGINE_OWNED_STORAGE_, owned )( member, Type )
// NOLINTEND(bugprone-macro-parentheses)
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

        /**
         * @brief 아직 없는 `owned=1` 서비스를 만듭니다. 생성만 하고 `initialize()` 는 부르지 않습니다.
         * @details **이미 있는 것은 건드리지 않습니다.** 호스트가 먼저 만들어야 하는 것이 있기 때문입니다.
         *          `EngineLoop` 는 명령줄을 파싱하려고 `CommandLineManager` 와 `GlobalVariableManager` 를
         *          이 호출보다 앞에서 만듭니다. 덮어썼다면 파싱 결과가 통째로 사라졌을 것입니다.
         *          나머지는 목록 순서로 만듭니다. 이 타입들의 생성자는 서로를 보지 않으므로 순서에 의미가
         *          없습니다. **의미가 있는 것은 초기화 순서**이고, 그것은 호스트가 적습니다.
         */
        void createAll();

        /**
         * @brief 만든 것을 서비스 표에 연결합니다. `owned=0` 자리는 **건드리지 않습니다.**
         * @details 그래서 호스트는 이 호출 전후 어느 쪽에서든 자기 몫(팩토리 · 조건부 생성)을 채울 수 있습니다.
         */
        void bindInto( EngineServices& outServices ) const
        {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )       SW_CONCAT( SW_ENGINE_OWNED_BIND_, owned )( member )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) SW_CONCAT( SW_ENGINE_OWNED_BIND_, owned )( member )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned )             SW_CONCAT( SW_ENGINE_OWNED_BIND_, owned )( member )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
        }

        /**
         * @brief 남은 소유를 모두 놓습니다.
         * @details 종료 절차에서 순서가 중요한 것은 호스트가 **먼저** 놓습니다. 이것은 그 뒤에 남은
         *          것을 쓸어 담는 자리입니다. 그래야 목록에 줄을 더한 사람이 종료 코드를 잊어도
         *          객체가 새지 않습니다.
         */
        void destroyAll();
    };
} // namespace sw
