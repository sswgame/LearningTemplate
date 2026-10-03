/**
 * @file Engine/EngineOwnedServices.h
 * @brief 호스트가 소유하는 엔진 서비스들의 **저장소**입니다. `EngineServiceList.xxx` 에서 생성됩니다.
 *
 * [왜 필요한가]
 * 서비스를 만들고 표에 연결하는 코드(멤버 선언 · `make_unique` · 대입)를 호스트(`EngineLoop` · 시험 하네스)마다 손으로 적으면,
 * 하나를 빠뜨렸을 때 `areEngineServicesBound()` 가 영영 false 가 되어 그것으로 게이팅되는 곳들이 조용히 폴백으로 갑니다.
 * 그래서 그 세 가지가 목록의 `EngineCreated` 한 글자에서 생성됩니다. 호스트는 이 구조체를 하나 들고
 * `createAll()` · `bindInto()` 만 부릅니다.
 *
 * [왜 `Common/` 이 아니라 루트인가]
 * 이 저장소는 서비스 스무 개의 **완전한 타입**을 압니다. `Engine/Common` 은 티어 0(엔진의 아무것도
 * 참조하지 않는 토대)이라 거기 두면 레이어 규칙(`CheckEngineLayers`)을 어깁니다.
 * 루트(`Source/Engine` 바로 아래)는 **"모두를 엮는 자리"** 이고 `EngineLoop` 가 있는 곳입니다.
 *
 * [초기화 · 종료 · 해제 순서는 여기 없다]
 * 이 저장소는 "누가 만들고 누가 들고 있는가" 만 압니다. 무엇을 언제 `initialize()` 하고 어떤 순서로
 * 내리고 해제하는지는 기동 단계 표(`Engine/EngineStartupStepList.xxx`)의 의존 칸이 정하고, `EngineStartupSequence` 가
 * 위상 정렬해 두 호스트(`EngineLoop` · 시험 하네스)에 같은 순서를 줍니다. 디바이스 · 렌더 스레드 · 모듈 이미지처럼
 * 서비스가 아닌 단계도 같은 표에 있어서 서비스 목록의 칸으로 두지 않았습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"

#if !defined( SW_ENGINE_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL ) && !defined( SW_TOOL_INTERNAL )
    #error "EngineOwnedServices.h can only be included internally by the Engine, App, or Tests."
#endif

// 인자가 **타입 이름과 선언자 이름**이라 괄호를 씌울 수 없다(EngineServices.h 와 같은 이유).
// NOLINTBEGIN(bugprone-macro-parentheses)
// creator 칸(`EngineCreated` · `HostCreated`)에 따라 갈라지는 자리들이다. `SW_CONCAT` 으로 낱말을 붙여 고른다. `if constexpr` 로는
// **선언**을 지울 수 없어서(멤버 선언과 헤더 포함이 걸린다) 이 방식이 필요하다.
#define SW_ENGINE_OWNED_STORAGE_HostCreated( member, Type )
#define SW_ENGINE_OWNED_STORAGE_EngineCreated( member, Type ) unique_ptr<Type> member{};
#define SW_ENGINE_OWNED_BIND_HostCreated( member )
#define SW_ENGINE_OWNED_BIND_EngineCreated( member ) outServices.member = member.get();
// NOLINTEND(bugprone-macro-parentheses)

namespace sw
{
    /**
     * @struct EngineOwnedServices
     * @brief `EngineCreated` 인 서비스의 소유권을 들고 있습니다. 호스트가 멤버로 하나 둡니다.
     * @note 소멸 순서는 **선언의 역순**, 즉 목록의 역순입니다(`destroyAll` 도 같습니다). 단계가 소유한 서비스는 호스트가
     *       기동 표의 해제로 먼저 놓습니다.
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
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       SW_CONCAT( SW_ENGINE_OWNED_STORAGE_, creator )( member, Type )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) SW_CONCAT( SW_ENGINE_OWNED_STORAGE_, creator )( member, Type )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                SW_CONCAT( SW_ENGINE_OWNED_STORAGE_, creator )( member, Type )
// NOLINTEND(bugprone-macro-parentheses)
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

        /**
         * @brief 아직 없는 `EngineCreated` 서비스를 만듭니다. 생성만 하고 `initialize()` 는 부르지 않습니다.
         * @details **이미 있는 것은 건드리지 않습니다.** 호스트가 먼저 만들어야 하는 것이 있기 때문입니다.
         *          `EngineLoop` 는 명령줄을 파싱하려고 `CommandLineManager` 와 `GlobalVariableManager` 를
         *          이 호출보다 앞에서 만듭니다. 덮어쓰면 파싱 결과가 통째로 사라집니다.
         *          나머지는 목록 순서로 만듭니다. 이 타입들의 생성자는 서로를 보지 않으므로 순서에 의미가
         *          없습니다. **의미가 있는 것은 초기화 순서**이고, 그것은 기동 단계 표(`EngineStartupStepList.xxx`)가 정합니다.
         */
        void createAll();

        /**
         * @brief 만든 것을 서비스 표에 연결합니다. `HostCreated` 자리는 **건드리지 않습니다.**
         * @details 그래서 호스트는 이 호출 전후 어느 쪽에서든 자기 몫(팩토리 · 조건부 생성)을 채울 수 있습니다.
         */
        void bindInto( EngineServices& outServices ) const
        {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       SW_CONCAT( SW_ENGINE_OWNED_BIND_, creator )( member )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) SW_CONCAT( SW_ENGINE_OWNED_BIND_, creator )( member )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                SW_CONCAT( SW_ENGINE_OWNED_BIND_, creator )( member )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
        }

        /**
         * @brief `ResourceManager` 를 종료하고 해제합니다. 호스트의 Resource 단계 해제(`destroy`)가 부릅니다.
         * @details 에셋 캐시를 비우므로 에셋을 든 다른 매니저가 모두 해제된 **뒤**여야 합니다. 표에서 Resource 는 Scene · Audio · Input ·
         *          렌더러보다 먼저 서므로 역순 해제에서 자연히 그 뒤입니다.
         */
        void destroyResourceManager();
        /**
         * @brief 코덱 레지스트리를 Core 슬롯에서 떼고 해제합니다. 호스트의 Compression 단계 해제가 부릅니다.
         * @details 슬롯이 이 레지스트리를 가리키면 먼저 비웁니다. 그 뒤의 압축 경로(`CompressionStream`)는 내장 코덱만 씁니다.
         */
        void destroyCompressionCodecRegistry();

        /**
         * @brief 남은 소유를 목록의 **역순**으로 모두 놓습니다(소멸자와 같은 순서).
         * @details 단계가 소유한 서비스는 호스트가 기동 표의 해제(`EngineStartupSequence::destroyAll`)로 먼저 놓습니다. 이것은 그 뒤에 남은
         *          표 밖 서비스(명령줄 · 전역 변수 · 이벤트 · 디버그 도구 등)를 쓸어 담는 자리입니다. 목록 앞줄의 명령줄 · 전역 변수가
         *          맨 나중에 사라집니다.
         */
        void destroyAll();
        /** @brief `destroyAll` 이 놓는 순서(멤버 이름)입니다. 같은 해제 표 · 같은 순서 함수로 만듭니다(시험이 순서를 본다). */
        static vector<const utf8*> makeDestroyOrder();
    };
} // namespace sw
