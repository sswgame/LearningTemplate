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
 * [규약]
 * - 컴포넌트 `.cpp` 에서 `onTick` 정의 뒤에 `SW_REGISTER_TICK_BATCH( 타입 )` 한 줄을 둡니다. `onTick` 이 공개여야 합니다(한정 이름으로 직접 부른다).
 * - **정확한 타입만** 일괄 경로로 갑니다. 파생 타입은 기반의 등록을 물려받지 않습니다(파생의 `onTick` 을 건너뛰지 않게) — 파생도 쓰려면 따로 등록합니다.
 * - 일괄 경로로 가는 것은 그 그룹에서 틱 항목이 **그 컴포넌트의 주 틱 하나뿐인** 오브젝트입니다. 오브젝트 안의 순서(그룹 · 순서 키 · 붙은 순서)와
 *   "한 오브젝트의 항목은 한 스레드가" 규칙이 그대로입니다. 항목이 둘 이상이면 오브젝트 칸 경로로 갑니다.
 * - 오브젝트 사이의 순서는 기본 경로와 같이 정하지 않습니다(병렬). 같은 입력이면 같은 목록이 지어집니다(목록은 등록 · 해제 순서로만 바뀐다).
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
#include "Core/String/RegistrationList.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Module/ModuleObjectRegistrar.h"
#include "Engine/Object/GameObject/Tick/StructuralChangeBuffer.h"
#include "Engine/Object/GameObject/Tick/TickRegistry.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @class TickBatchRegistry
     * @brief 타입 → 일괄 틱 함수의 레지스트리입니다(서비스 범위의 서브시스템 — `EngineSubsystemSet`). 파일 머리말 참고.
     * @details 씬은 생성 때 받은 서비스 묶음의 칸(`SceneEngineServices::_pTickBatchRegistry`)으로 읽는다 — 칸이 비면(엔진 없는 테스트 매니저) 일괄 경로가 없다.
     * @note 조회(`findFunction`)는 게임 스레드에서 틱 밖(`TickRegistry::refresh`)에만 합니다. 등록 · 해제는 모듈을 올리고 내리는 스레드에서 옵니다.
     */
    class SW_API TickBatchRegistry final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "TickBatchRegistry";

        /** @brief 타입의 `StaticType` 입니다. 주소가 모듈 재적재로 바뀔 수 있어 조회할 때마다 부릅니다. */
        using StaticTypeFunction = const TypeInfo* (*)();

        /**
         * @struct Entry
         * @brief 등록 줄 하나입니다. 등록하는 객체(`TickBatchRegistration<T>`)가 멤버로 들고, 레지스트리는 그 주소를 공용 등록 목록(`RegistrationList`)에 올립니다.
         * @details 줄이 레지스트리에 있는 동안 살아 있고 옮겨지지 않아야 합니다 — 등록 객체가 정적 객체(모듈의 수명)이거나 테스트의 지역 `Entry` 입니다.
         */
        struct Entry
        {
            StaticTypeFunction _pStaticType; ///< 타입의 `StaticType`
            TickBatchFunction  _pFunction;   ///< 그 타입의 일괄 틱 함수
        };

        // ISubsystem — 세울 것이 없다(줄은 모듈을 올리는 길이 붙인다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 줄 하나를 등록합니다. 세대를 올립니다. 같은 함수의 줄이 둘 오를 수 있으며(정적 등록자와 테스트의 지역 줄), 각자 자기 줄만 지웁니다. */
        void registerType( Entry& entry ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 자기 줄의 등록을 지웁니다(모듈 해제). 오르지 않은 줄이면 아무것도 하지 않습니다. 세대를 올립니다. */
        void unregisterType( Entry& entry ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 정확히 @p pType 인 컴포넌트의 일괄 틱 함수입니다. 등록이 없으면 nullptr 입니다. 나중에 오른 줄이 먼저다(테스트의 지역 줄이 정적 등록을 덮는다). */
        TickBatchFunction findFunction( const TypeInfo* pType ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 등록 · 해제마다 오르는 세대입니다. `TickRegistry` 가 바뀐 것을 보고 항목을 다시 짓습니다. */
        uint32 getGeneration() const { return _generation.load( std::memory_order_acquire ); }
        /** @brief 등록된 타입 수입니다(진단 · 테스트). */
        uint32 getRegisteredCount() SW_EXCLUDES_LOCK( _mutex );

    private:
        mutex                              _mutex;
        RegistrationList<Entry> _listEntry SW_GUARDED_BY( _mutex ){ "TickBatchRegistry" };
        atomic<uint32>                     _generation{ 0 };
    };

    /**
     * @class TickBatchRegistrar
     * @brief 일괄 틱 줄 하나의 정적 등록자입니다(`TickBatchRegistration<T>` 의 바탕). 모듈을 올린 쪽이 붙이고(`attach`) 내리기 전에 뗍니다(`detach`).
     * @details 만들 때는 링크 때 목록(`ModuleObjectRegistrar::getHead`)에만 달린다 — 레지스트리는 서비스 범위에 있어 정적 초기화 때는 아직 없다.
     */
    class SW_API TickBatchRegistrar : public ModuleObjectRegistrar
    {
    public:
        void attach() override;
        void detach() override;

    protected:
        explicit TickBatchRegistrar( const TickBatchRegistry::Entry& entry )
            : _entry{ entry }
        {
        }
        ~TickBatchRegistrar() = default;

    private:
        TickBatchRegistry::Entry _entry; ///< 레지스트리의 등록 목록에 오르는 줄 — 붙인 동안 오른다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 타입 @p TComponent 의 일괄 틱 함수입니다. `SW_REGISTER_TICK_BATCH` 가 그 타입의 `.cpp` 에서 인스턴스화합니다(`onTick` 이 인라인될 수 있게).
     * @details 원소마다 기본 경로(`runTickItemIfLive`)와 같은 검사(삭제 대기 · 자기 활성 · 주 틱 참여)를 하고 `TComponent::onTick` 을 한정 이름으로
     *          부릅니다. 레지스트리가 정확한 타입만 넣으므로 가상 호출과 같은 함수입니다.
     */
    template <typename TComponent>
    struct TickBatchOf
    {
        static void tickRange( TickThreadState& threadState, const TickBatchElement* pElement, uint32 count, float32 deltaTime )
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                const TickBatchElement& element = pElement[index];
                TComponent*             pComp   = static_cast<TComponent*>( element._pComponent );
                threadState._pTickingObject     = element._pObject;
                threadState._pTickWriter        = element._pObject;
                const bool bSkipTick            = pComp->isPendingDestroy() || pComp->isSelfActive() == false || pComp->canEverTick() == false;
                if ( bSkipTick )
                    continue;
                pComp->TComponent::onTick( deltaTime );
            }
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 정적 객체 하나로 타입을 등록합니다 — 모듈을 올릴 때 붙고 내리기 전에 떨어진다(`TickBatchRegistrar`). `SW_REGISTER_TICK_BATCH` 가 만듭니다.
     * @details 링크 때 목록에 달리는 객체라 정적 객체로만 둔다. 테스트처럼 범위 안에서만 올리려면 지역 `TickBatchRegistry::Entry` 를 레지스트리에 직접 올린다.
     */
    template <typename TComponent>
    class TickBatchRegistration final : public TickBatchRegistrar
    {
    public:
        TickBatchRegistration()
            : TickBatchRegistrar{
                  TickBatchRegistry::Entry{ &TComponent::StaticType, &TickBatchOf<TComponent>::tickRange }
        }
        {
        }

        TickBatchRegistration( const TickBatchRegistration& )            = delete;
        TickBatchRegistration& operator=( const TickBatchRegistration& ) = delete;
    };
} // namespace sw

/**
 * @brief 컴포넌트 타입을 일괄 틱 경로에 올립니다. 그 타입의 `.cpp` 에서 `onTick` 정의 뒤, `namespace sw` 안에 `SW_REGISTER_TICK_BATCH( 타입 );` 로 한 번 씁니다.
 * @details 정적 등록 객체가 모듈을 올릴 때 등록되고 내리기 전에 지워집니다(`TickBatchRegistrar`).
 */
#define SW_REGISTER_TICK_BATCH( Type )                                     \
    static ::sw::TickBatchRegistration<Type> s_tickBatchRegistration##Type \
    {                                                                      \
    }
