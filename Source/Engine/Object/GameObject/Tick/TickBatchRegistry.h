/**
 * @file TickBatchRegistry.h
 * @brief 타입별 일괄 틱의 등록부입니다 — 한 타입의 `onTick` 을 가상 호출 없이 연속 배열로 묶어 도는 함수(`TickBatchFunction`)를 타입마다 하나 둡니다.
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
 * @note 등록 · 해제는 모듈 적재 · 해제(정적 초기화 · 소멸)와 함께 일어납니다. 세대(`getGeneration`)가 오르면 `TickRegistry` 가 다음 틱 전에 모든 오브젝트의
 *       항목을 다시 지어, 내려간 모듈의 함수를 들고 있지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/GameObject/Tick/StructuralChangeBuffer.h"
#include "Engine/Object/GameObject/Tick/TickRegistry.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @class TickBatchRegistry
     * @brief 타입 → 일괄 틱 함수의 프로세스 전역 등록부입니다. 파일 머리말 참고.
     * @note 조회(`findFunction`)는 게임 스레드에서 틱 밖(`TickRegistry::refresh`)에만 합니다. 등록 · 해제는 모듈의 정적 초기화 · 소멸에서 옵니다.
     */
    class SW_API TickBatchRegistry
    {
    public:
        /** @brief 타입의 `StaticType` 입니다. 주소가 모듈 재적재로 바뀔 수 있어 조회할 때마다 부릅니다. */
        using StaticTypeFunction = const TypeInfo* (*)();

        /** @brief 타입 하나의 일괄 틱 함수를 등록합니다. 같은 함수를 두 번 등록하면 앞의 것을 바꿉니다. 세대를 올립니다. */
        static void registerType( StaticTypeFunction pStaticType, TickBatchFunction pFunction );
        /** @brief 등록을 지웁니다(모듈 해제). 없으면 아무것도 하지 않습니다. 세대를 올립니다. */
        static void unregisterType( TickBatchFunction pFunction );
        /** @brief 정확히 @p pType 인 컴포넌트의 일괄 틱 함수입니다. 등록이 없으면 nullptr 입니다. */
        static TickBatchFunction findFunction( const TypeInfo* pType );
        /** @brief 등록 · 해제마다 오르는 세대입니다. `TickRegistry` 가 바뀐 것을 보고 항목을 다시 짓습니다. */
        static uint32 getGeneration();
        /** @brief 등록된 타입 수입니다(진단 · 테스트). */
        static uint32 getRegisteredCount();
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 타입 @p TComponent 의 일괄 틱 함수입니다. `SW_REGISTER_TICK_BATCH` 가 그 타입의 `.cpp` 에서 인스턴스화합니다(`onTick` 이 인라인될 수 있게).
     * @details 원소마다 기본 경로(`runTickItemIfLive`)와 같은 검사(삭제 대기 · 자기 활성 · 주 틱 참여)를 하고 `TComponent::onTick` 을 한정 이름으로
     *          부릅니다. 등록부가 정확한 타입만 넣으므로 가상 호출과 같은 함수입니다.
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
     * @brief 정적 객체 하나로 타입을 등록하고, 모듈이 내려갈 때(정적 소멸) 지웁니다. `SW_REGISTER_TICK_BATCH` 가 만듭니다.
     */
    template <typename TComponent>
    class TickBatchRegistration
    {
    public:
        TickBatchRegistration() { TickBatchRegistry::registerType( &TComponent::StaticType, &TickBatchOf<TComponent>::tickRange ); }
        ~TickBatchRegistration() { TickBatchRegistry::unregisterType( &TickBatchOf<TComponent>::tickRange ); }

        TickBatchRegistration( const TickBatchRegistration& )            = delete;
        TickBatchRegistration& operator=( const TickBatchRegistration& ) = delete;
    };
} // namespace sw

/**
 * @brief 컴포넌트 타입을 일괄 틱 경로에 올립니다. 그 타입의 `.cpp` 에서 `onTick` 정의 뒤, `namespace sw` 안에 `SW_REGISTER_TICK_BATCH( 타입 );` 로 한 번 씁니다.
 * @details 정적 등록 객체가 모듈 적재 때 등록하고 해제 때 지웁니다. 테스트처럼 범위 안에서만 올리려면 `TickBatchRegistration<타입>` 지역 객체를 씁니다.
 */
#define SW_REGISTER_TICK_BATCH( Type )                                           \
    static const ::sw::TickBatchRegistration<Type> s_tickBatchRegistration##Type \
    {                                                                            \
    }
