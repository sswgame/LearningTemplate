/**
 * @file TickBatchRegistry.h
 * @brief 타입별 일괄 틱의 레지스트리입니다 — 한 타입의 `onTick` 을 가상 호출 없이 연속 배열로 묶어 도는 함수(`TickBatchFunction`)를 타입마다 하나 둡니다.
 *
 * [왜 필요한가]
 * 기본 틱 경로는 오브젝트 칸 목록(`TickObjectEntry`, 40 B)을 돌며 칸마다 컴포넌트 포인터를 따라가 `onTick` 을 가상 호출합니다. 같은 타입의 컴포넌트가
 * 수천 개(무버 · 투사체)면 타입이 같다는 것을 알면서도 매번 칸 → 컴포넌트 → vtable 을 지납니다. 등록한 타입은 레지스트리(`TickRegistry`)가 그 타입의
 * (컴포넌트, 오브젝트) 연속 배열(16 B)에 따로 모으고, 디스패치는 배열 한 구간을 **그 타입의 함수 하나**에 넘깁니다. 언리얼 Mass 의 프로세서 · 유니티
 * `IJobEntity` 가 같은 종류를 한 번에 도는 것과 같은 자리이고, 타입이 자기 데이터를 연속 배열로 옮기는 다음 단계(SoA)의 입구입니다.
 *
 * [규약 — 리플렉션 생성기(5-4)와 맞춘 등록 API]
 * - 등록 한 줄은 `SW_REGISTER_TICK_TYPE( 타입, 모드 )` 입니다(모드는 `TickMode` 의 `Batch` · `Task` · `Main`). 생성기가 `onTick` 을 재정의한 타입과
 *   `REFLECT( TickMode = … )` 를 단 타입마다 생성 `.cpp` 의 `namespace sw` 안에 냅니다(메타데이터가 없으면 `Batch`). 손으로 쓸 때도 같은 줄이다.
 *   `TickBatchOf<타입>` 은 `onTick` · `onSubTick` 을 `ReflectAccess<TickAccessTag>` 로 한정 이름 호출한다(`REFLECT_BODY` 가 그 통로를 친구로 연다).
 *   `TickMode` 를 적지 않은 형식은 생성기가 `makeDeclaredTickEntry` 줄을 내고, 직접 선언했는지는 컴파일에서 정한다.
 * - **함수는 정확한 타입만** 씁니다. 파생 타입은 기반의 함수를 물려받지 않습니다(파생의 `onTick` 을 건너뛰지 않게) — 등록이 없는 타입은 가상 호출
 *   함수(`tickBatchVirtual`)로 같은 파도를 탑니다. **모드는 물려받습니다** — 등록이 없는 파생은 가장 가까운 등록된 조상의 모드이다(Main 기반의 파생이 워커로 새지 않게).
 * - 서브틱 · 여러 항목 오브젝트도 파도로 일괄합니다(`TickRegistry` 머리말). 선행 조건을 건 항목만 모드과 무관하게 스테이지로 갑니다.
 * - 오브젝트 사이의 순서는 정하지 않습니다(병렬). 같은 입력이면 같은 목록이 지어집니다(목록은 등록 · 해제 순서로만 바뀐다).
 *
 * [컴포넌트 틱의 잡 컨텍스트(NEXT 5-3)]
 * - 서명: 주 틱은 `onTick( TickContext& context )`, 서브틱은 `onSubTick( uint32 subTickID, TickContext& context )` 다(옛 `float32` 서명은 지웠다).
 *   컨텍스트는 `SceneJobContext`(매니저 읽기 전용 · 시간 · 틱 뒤 큐)에 자기 오브젝트(`getSelf`)를 더한 것이다(`TickContext.h`).
 *   디스패치(`TickBatchOf` · `tickBatchVirtual` · `runTickItem`)가 원소마다 스택에 세운다. 유니티 `IJobEntity` + `EntityCommandBuffer`,
 *   언리얼 `FMassExecutionContext::Defer()` 와 같은 자리다.
 * - 쓰기 가능한 매니저: `GameObject::getManager` 는 const 이고 쓰기는 `getMutableManager` 로 가른다(`TickContext.h`). 틱 본문은 읽기를
 *   `context.getManager()` 로, 남의 오브젝트 쓰기를 `context.deferPostTick` · `deferPostTickOnComponent` 로 한다. 남은 틈은 NEXT 5-3.
 *
 * @note 등록 · 해제는 모듈을 올리고 내리는 길에서 일어납니다 — 정적 등록 객체는 만들 때 링크 때 목록(`ModuleObjectRegistrar`, E1 사전 등록 사슬)에만 달리고,
 *       `engine::registerModuleTypes` 가 걷어 서비스 범위의 레지스트리에 붙이며(`attach`), `unregisterModuleTypes` 가 그 모듈의 컴포넌트를 지우기 전에 뗍니다(`detach`).
 *       세대(`getGeneration`)가 오르면 `TickRegistry` 가 다음 틱 전에 모든 오브젝트의 항목을 다시 지어, 내려간 모듈의 함수를 들고 있지 않습니다.
 *       표는 서비스 범위의 서브시스템이 갖는다 — 예전에는 함수 안 정적 목록이라 엔진을 다시 세워도 남았다. 언리얼이 모듈의 클래스를 올릴 때 Mass 프로세서를
 *       레지스트리에 더하고 내릴 때 빼는 자리와 같다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Module/ModuleEntryRegistrar.h"
#include "Engine/Object/GameObject/Tick/StructuralChangeBuffer.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/Tick/TickRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @class TickBatchRegistry
     * @brief 타입 → 일괄 틱 함수의 레지스트리입니다(서비스 범위의 서브시스템 — `EngineSubsystemSet`). 파일 머리말 참고.
     * @details 씬은 생성 때 받은 서비스 묶음의 칸(`SceneEngineServices::_pTickBatchRegistry`)으로 읽는다 — 칸이 비면(엔진 없는 테스트 매니저) 일괄 경로가 없다.
     * @note 조회(`findFunction`)는 게임 스레드에서 틱 밖(`TickRegistry::refresh`)에만 합니다. 등록 · 해제는 모듈을 올리고 내리는 스레드에서 옵니다.
     */
    class SW_API TickBatchRegistry final : public Subsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "TickBatchRegistry";

        /** @brief 타입의 `StaticType` 입니다. 주소가 모듈 재적재로 바뀔 수 있어 조회할 때마다 부릅니다. */
        using StaticTypeFunction = const TypeInfo* (*)();

        /**
         * @struct Entry
         * @brief 등록 줄 하나입니다. 등록자(`TickBatchRegistrar`)가 멤버로 들고, 레지스트리는 그 주소를 공용 등록 목록(`RegistrationList`)에 올립니다.
         * @details 줄이 레지스트리에 있는 동안 살아 있고 옮겨지지 않아야 합니다 — 등록 객체가 정적 객체(모듈의 수명)이거나 테스트의 지역 `Entry` 입니다.
         */
        struct Entry
        {
            StaticTypeFunction _pStaticType; ///< 타입의 `StaticType`
            TickBatchFunction  _pFunction;   ///< 그 타입의 일괄 틱 함수(Batch 모드가 쓴다)
            TickMode           _mode;        ///< 그 타입이 선언한 모드
        };

        // Subsystem — 세울 것이 없다(줄은 모듈을 올리는 길이 붙인다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 줄 하나를 등록합니다. 세대를 올립니다. 같은 함수의 줄이 둘 오를 수 있으며(정적 등록자와 테스트의 지역 줄), 각자 자기 줄만 지웁니다. 타입이 비었거나 Batch 줄의 함수가 비었으면 false 입니다. */
        [[nodiscard]] bool registerEntry( Entry& entry ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 자기 줄의 등록을 지웁니다(모듈 해제). 오르지 않은 줄이면 아무것도 하지 않습니다. 세대를 올립니다. */
        void unregisterEntry( Entry& entry ) SW_EXCLUDES_LOCK( _mutex );
        /**
         * @brief @p pType 컴포넌트의 모드과 일괄 함수입니다. 나중에 오른 줄이 먼저다(테스트의 지역 줄이 정적 등록을 덮는다).
         * @details 함수는 정확히 @p pType 의 줄에서만 온다. 모드는 @p pType 부터 조상으로 올라가 처음 등록된 타입의 것이고, 아무도 없으면 Batch 다.
         */
        TickTypeDispatch findDispatch( const TypeInfo* pType ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 등록 · 해제마다 오르는 세대입니다. `TickRegistry` 가 바뀐 것을 보고 항목을 다시 짓습니다. */
        uint32 getGeneration() const { return _generation.load( std::memory_order_acquire ); }
        /** @brief 등록된 타입 수입니다(진단 · 테스트). */
        uint32 getRegisteredCount() SW_EXCLUDES_LOCK( _mutex );

    private:
        mutex                              _mutex;
        RegistrationList<Entry> _listEntry SW_GUARDED_BY( _mutex ){ "TickBatchRegistry" };
        atomic<uint32>                     _generation{ 0 };

        SW_SERVICE_SLOT( TickBatchRegistry, Required );
    };

    /**
     * @brief 일괄 틱 줄 하나의 정적 등록자입니다. 줄은 이 등록자가 들고, 붙은 동안 레지스트리의 등록 목록에 오릅니다.
     * @details 링크 때 목록에 달리는 객체라 정적 객체로만 둔다. 테스트처럼 범위 안에서만 올리려면 지역 `TickBatchRegistry::Entry` 를 레지스트리에 직접 올린다.
     */
    using TickBatchRegistrar = ModuleEntryRegistrar<TickBatchRegistry, TickBatchRegistry::Entry>;
} // namespace sw

namespace sw
{
    /**
     * @brief 원소 하나를 틱할지 봅니다 — 기본 경로(`runTickItemIfLive` · `runTickItem`)와 같은 검사(삭제 대기 · 자기 활성 · 주 틱 참여 또는 서브틱 실행 여부)입니다.
     * @details 서브틱 등록 · 해제 · 활성 변경은 틱 뒤로 미뤄지고, 실행 여부(원자 마스크 · `_bRunnable`)만 틱 중에 바로 바뀐다 — 그래서 원소마다 본다.
     *          템플릿인 까닭은 이 헤더가 `Component.h` 를 끌어오지 않게(부르는 쪽이 완전한 타입을 안다)다.
     */
    template <typename TComponent>
    bool shouldRunTickElement( const TComponent& comp, uint32 subTickID )
    {
        if ( comp.isPendingDestroy() || comp.isSelfActive() == false )
            return false;
        return subTickID == 0 ? comp.canEverTick() : comp.isSubTickActive( subTickID );
    }

    /** @brief 일괄 틱이 리플렉트 형식의 틱 함수에 닿는 통로의 태그입니다(`ReflectAccess<TickAccessTag>`). */
    struct TickAccessTag;

    /**
     * @brief 일괄 틱의 통로 — 틱 함수를 한정 이름으로(비가상) 부르고, 형식이 틱 함수를 **직접 선언했는지** 컴파일에서 답한다.
     * @details `REFLECT_BODY()` 가 친구로 여는 자리라 protected · private 재정의도 본다. 직접 선언 판정은 멤버 포인터의 클래스 형식으로 한다
     *          (`&T::onTick` 이 `void ( T::* )( TickContext& )` 면 T 가 선언했다). 기반의 private 재정의처럼 이름이 닿지 않으면 직접 선언이 아니다(SFINAE).
     */
    template <>
    struct ReflectAccess<TickAccessTag>
    {
        template <typename TComponent>
        static void onTick( TComponent& comp, TickContext& context )
        {
            comp.TComponent::onTick( context );
        }

        template <typename TComponent>
        static void onSubTick( TComponent& comp, const uint32 subTickID, TickContext& context )
        {
            comp.TComponent::onSubTick( subTickID, context );
        }

        /** @brief @p TComponent 가 `onTick` 이나 `onSubTick` 을 직접 선언했으면 true 입니다(물려받은 것은 세지 않는다 — 일괄 함수는 정확한 타입의 것이다). */
        template <typename TComponent>
        static constexpr bool hasDeclaredTick() noexcept
        {
            return hasDeclaredOnTick<TComponent>( 0 ) || hasDeclaredOnSubTick<TComponent>( 0 );
        }

    private:
        template <typename TComponent>
        static constexpr auto hasDeclaredOnTick( int ) noexcept -> decltype( &TComponent::onTick, bool() )
        {
            return std::is_same_v<decltype( &TComponent::onTick ), void ( TComponent::* )( TickContext& )>;
        }
        template <typename TComponent>
        static constexpr bool hasDeclaredOnTick( ... ) noexcept
        {
            return false;
        }
        template <typename TComponent>
        static constexpr auto hasDeclaredOnSubTick( int ) noexcept -> decltype( &TComponent::onSubTick, bool() )
        {
            return std::is_same_v<decltype( &TComponent::onSubTick ), void ( TComponent::* )( uint32, TickContext& )>;
        }
        template <typename TComponent>
        static constexpr bool hasDeclaredOnSubTick( ... ) noexcept
        {
            return false;
        }
    };

    /**
     * @brief 타입 @p TComponent 의 일괄 틱 함수입니다. `SW_REGISTER_TICK_TYPE` 이 그 타입의 등록 TU 에서 인스턴스화합니다(`onTick` 이 인라인될 수 있게).
     * @details 원소마다 `shouldRunTickElement` 를 보고 `TComponent::onTick` / `TComponent::onSubTick` 을 한정 이름으로 부릅니다. 레지스트리가 정확한
     *          타입만 넣으므로 가상 호출과 같은 함수입니다. 부를 때마다 @p TScopeName 구간(타입 이름 — 구간 하나 = 워커 청크 하나)을 엽니다.
     */
    template <typename TComponent, const utf8* TScopeName>
    struct TickBatchOf
    {
        static void tickRange( TickThreadState& threadState, const TickBatchElement* pElement, uint32 count, uint32 subTickID, float32 deltaTime )
        {
            SW_PROFILE_SCOPE( TScopeName );
            for ( uint32 index = 0; index < count; ++index )
            {
                const TickBatchElement& element = pElement[index];
                TComponent*             pComp   = static_cast<TComponent*>( element._pComponent );
                threadState._pTickingObject     = element._pObject;
                threadState._pTickWriter        = element._pObject;
                if ( shouldRunTickElement( *pComp, subTickID ) == false )
                    continue;
                TickContext context{ *element._pObject, deltaTime };
                if ( subTickID == 0 )
                    ReflectAccess<TickAccessTag>::onTick( *pComp, context );
                else
                    ReflectAccess<TickAccessTag>::onSubTick( *pComp, subTickID, context );
            }
        }
    };

    /**
     * @brief 일괄 함수가 등록되지 않은 타입(생성기가 아직 내지 않은 타입 · 테스트 타입 · 모드만 물려받은 파생)의 Batch 모드 함수입니다 — 원소마다 가상 호출합니다.
     * @details Debug 카운터 `GT.Scene.tick.virtualTicks` 를 센다(5-3 측정 기반 — 생성기가 덮을수록 줄어든다).
     */
    SW_API void tickBatchVirtual( TickThreadState& threadState, const TickBatchElement* pElement, uint32 count, uint32 subTickID, float32 deltaTime );

    /**
     * @brief `REFLECT( TickMode )` 를 적지 않은 컴포넌트의 등록 줄입니다 — 리플렉션 생성기가 추상이 아닌 컴포넌트마다 냅니다.
     * @details 그 형식이 `onTick` · `onSubTick` 을 **직접 선언했을 때만** Batch 줄이 된다(컴파일 판정, `ReflectAccess<TickAccessTag>::hasDeclaredTick`). 아니면 형식이 빈 줄이라
     *          레지스트리가 받지 않는다 — 물려받은 형식은 가까운 등록 조상의 모드를 따르고 가상 호출로 돈다(Main 기반의 파생이 Batch 로 새지 않게).
     *          판정을 생성기가 이름 · 인자 수로 내리던 것을 컴파일러에게 넘겼다. 직접 선언하지 않은 형식은 일괄 함수를 인스턴스화하지 않는다.
     */
    template <typename TComponent, const utf8* TScopeName>
    TickBatchRegistry::Entry makeDeclaredTickEntry()
    {
        if constexpr ( ReflectAccess<TickAccessTag>::hasDeclaredTick<TComponent>() )
            return TickBatchRegistry::Entry{ &TComponent::StaticType, &TickBatchOf<TComponent, TScopeName>::tickRange, TickMode::Batch };
        else
            return TickBatchRegistry::Entry{ nullptr, nullptr, TickMode::Batch };
    }
} // namespace sw

/**
 * @brief 컴포넌트 타입의 틱 모드과 일괄 함수를 올립니다 — 리플렉션 생성기가 부르는 등록 함수입니다(`REFLECT( TickMode = Batch|Task|Main )`, 없으면 Batch).
 * @details 그 타입의 생성 `.cpp`(또는 손으로 쓸 때 그 타입의 `.cpp`)의 `namespace sw` 안에 `SW_REGISTER_TICK_TYPE( 타입, Batch );` 로 한 번 씁니다.
 *          정적 등록 객체가 모듈을 올릴 때 등록되고 내리기 전에 지워집니다(`TickBatchRegistrar`). 등록한 함수는 타입 이름의 계측 구간
 *          (`GT.Scene.tickBatch.<타입>`)을 엽니다(`TickBatchOf`). Task · Main 모드 줄도 같은 함수를 든다(모드가 바뀌어도 줄 모양이 같게).
 */
#define SW_REGISTER_TICK_TYPE( Type, Mode )                                                                         \
    static constexpr utf8           kTickBatchScopeName##Type[] = "GT.Scene.tickBatch." #Type;                      \
    static ::sw::TickBatchRegistrar s_tickBatchRegistration##Type                                                   \
    {                                                                                                               \
        ::sw::TickBatchRegistry::Entry                                                                              \
        {                                                                                                           \
            &Type::StaticType, &::sw::TickBatchOf<Type, kTickBatchScopeName##Type>::tickRange, ::sw::TickMode::Mode \
        }                                                                                                           \
    }
