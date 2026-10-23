/**
 * @file BehaviorTaskRegistry.h
 * @brief 행동 트리 데이터 파일(`*.bt.xml`)이 이름으로 가리키는 작업 · 조건 함수의 레지스트리와 등록 줄 `SW_BEHAVIOR_TASK` 를 정의합니다.
 * @details 트리의 모양(노드 종류 · 블랙보드 조건 · 중단 규칙)은 데이터이고, 잎이 하는 일(작업 · 조건 함수)은 코드다. 데이터는 함수 포인터를 가질 수 없으므로 이름을 적고,
 *          그 이름을 함수에 잇는 표가 이 레지스트리다. 이름이 없으면 로더가 오류로 멈춘다(함수로 조용히 바꾸지 않는다). 모듈을 내리면 그 모듈의 함수 이름이 빠진다 —
 *          함수 포인터가 게임 모듈 것이어서 핫 리로드 뒤에는 트리를 다시 읽어야 한다는 `BehaviorTree` 의 규칙과 같은 이유다.
 *          이름은 `<영역>.<작업>` 소문자로 쓴다(`rts.defend`). 대소문자는 가리지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/String/RegistrationList.h"

#include "GameFramework/Base/Actor/AI/BehaviorTree.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 작업 · 조건 함수 하나의 등록 줄입니다. 문자열은 등록한 이미지(모듈)의 정적 저장소에 있습니다. */
    struct SW_GF_API BehaviorTaskEntry
    {
        const utf8*          _pName;    ///< 데이터 파일이 `task` 속성으로 적는 이름입니다(`rts.defend`).
        BehaviorTaskFunction _function; ///< 작업 또는 조건 함수입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class BehaviorTaskRegistry
     * @brief 이름 → 작업 · 조건 함수 표입니다(GameFramework 이미지에 하나 있으며, 키트 · 게임 모듈이 등록하고 모듈을 내리면 빠집니다).
     * @details 등록 · 해제는 이미지 로더 스레드(정적 초기화)에서, 조회는 트리를 읽는 스레드에서 오므로 목록은 잠금 하나로 지킵니다. 프레임마다 도는 경로가 아닙니다(트리를 읽을 때만 찾는다).
     */
    class SW_GF_API BehaviorTaskRegistry
    {
    public:
        /** @brief 프로세스에 하나인 레지스트리입니다. */
        static BehaviorTaskRegistry& get();

        BehaviorTaskRegistry();

        /** @brief 함수를 올립니다. 이름이 비었거나 이미 있거나 함수가 없으면 false 입니다(오류를 남긴다). */
        bool registerTask( const BehaviorTaskEntry* pEntry );
        /** @brief 함수를 뺍니다(없으면 아무 일도 없습니다). */
        void unregisterTask( const BehaviorTaskEntry* pEntry );
        /** @brief 이름(대소문자 무시)의 함수입니다. 없으면 nullptr 입니다. */
        BehaviorTaskFunction findTask( string_view name ) const;
        /** @brief 등록된 모든 이름을 ", " 로 이은 글입니다(모르는 이름 오류에 보인다). */
        string joinNames() const;
        /** @brief 등록된 함수 수입니다. */
        uint32 getCount() const;

    private:
        mutable mutex                             _mutex;        ///< 등록 목록을 지킨다
        RegistrationList<const BehaviorTaskEntry> _registration; ///< 이름은 대소문자를 무시하고, 이름 없는 등록은 거절한다
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_BEHAVIOR_TASK` 가 두는 정적 등록자입니다 — 만들 때 올리고 없앨 때(모듈을 내릴 때) 뺍니다. */
    struct SW_GF_API BehaviorTaskRegistrar
    {
        explicit BehaviorTaskRegistrar( const BehaviorTaskEntry* pEntry );
        ~BehaviorTaskRegistrar();
        BehaviorTaskRegistrar( const BehaviorTaskRegistrar& )            = delete;
        BehaviorTaskRegistrar& operator=( const BehaviorTaskRegistrar& ) = delete;

        const BehaviorTaskEntry* _pEntry;
        bool                     _bRegistered;
    };
} // namespace sw

/**
 * @brief 행동 트리 작업 · 조건 함수를 그 함수의 .cpp 에서 이름으로 등록합니다(`namespace sw` 안, 파일 범위). 모든 구성(Shipping 포함)에 남습니다.
 * @param varName   파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pName     데이터 파일이 적는 이름(리터럴, `<영역>.<작업>`)
 * @param pFunction `BehaviorStatus( BehaviorContext& )` 함수
 */
#define SW_BEHAVIOR_TASK( varName, pName, pFunction )                                       \
    static const ::sw::BehaviorTaskEntry     sw_behaviorTask_##varName{ pName, pFunction }; \
    static const ::sw::BehaviorTaskRegistrar sw_behaviorTaskRegistrar_##varName             \
    {                                                                                       \
        &sw_behaviorTask_##varName                                                          \
    }
