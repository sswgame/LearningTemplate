/**
 * @file ActionRegistry.h
 * @brief 동작의 등록 줄(`ActionEntry`)과 이름 → 동작 표, 등록 매크로 `SW_GAME_ACTION`, 동작 호출 추적입니다. 읽은 호출을 들기만 하는 쪽은 `GameActionCall.h` 만 본다.
 * @details 칸은 리플렉션 구조체라 고정 칸이 없고, 편집기는 등록을 읽어 목록과 칸을 저절로 보인다.
 *          어빌리티 이펙트(복제 · 예측), 기믹 회로 노드(스텝 상태), `EditorCommandRegistry` · `DevCommandRegistry`(게임플레이가 아니다)는 여기에 합치지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/NamedEntryRegistry.h"

#include "Engine/Behavior/GameActionCall.h"
#include "Engine/Reflection/InstancedStruct.h"

namespace sw
{
    /** @brief 형식을 지운 동작 함수입니다. */
    using ActionFunction = ActionResult ( * )( ActionContext& context, const void* pParams );

    /** @brief 동작 이름 하나의 등록 줄입니다. 글자는 등록한 이미지의 정적 저장소에 있습니다. */
    struct ActionEntry
    {
        static constexpr const utf8* kEntryKind = "Game action";

        const utf8*               _pName;
        const InstancedStructOps* _pParamsOps;
        ActionFunction            _function;

        const utf8* getEntryName() const { return _pName; }
        /** @brief 등록 목록입니다(Engine 이미지의 함수 정적 — 까닭은 `ConditionEntry::getEntries` 와 같다). 이름은 대소문자를 보지 않는다. */
        SW_API static LockedRegistrationList<const ActionEntry>& getEntries();
    };

    /** @brief `ActionResult( ActionContext&, const TParams& )` 함수를 형식을 지운 함수로 옮깁니다. */
    template <typename TParams, auto pFunction>
    struct ActionThunk
    {
        static ActionResult call( ActionContext& context, const void* pParams )
        {
            return pFunction( context, *static_cast<const TParams*>( pParams ) );
        }
    };

    /** @brief 이름 → 동작 표입니다(대소문자 무시). */
    using ActionRegistry = NamedEntryRegistry<ActionEntry>;

    /** @brief 동작 호출 하나의 추적 줄입니다(범용 Action 런타임 디버거 — HUD 섹션 `action`). 이름은 베껴 둔다(모듈을 내려도 남는다). */
    struct ActionTraceEntry
    {
        static constexpr uint32 kNameCapacity = 48;

        utf8         _arrName[kNameCapacity]{}; ///< 동작 이름(넘치면 자른다)
        uint64       _sequence{ 0 };            ///< 켠 뒤 몇 번째 호출인지
        ActionResult _result{ ActionResult::Succeeded };
        uint8        _bJustStarted{ SW_FALSE }; ///< 실행기가 이번에 시작한 호출
        uint8        _bAborted{ SW_FALSE };     ///< 중단으로 불린 호출
    };

    /**
     * @struct ActionTrace
     * @brief 모든 동작 호출(대화 · 스케줄 · 보상 · 행동 트리 · 퀘스트 — 모두 `GameActionCall::invoke` 를 지난다)의 고정 용량 추적입니다.
     * @details 꺼져 있으면 호출마다 원자 읽기 하나다(`action.trace on|off`). 켜면 잠금 하나로 고리 `kCapacity` 칸에 이름 · 결과 · 시작/중단을 적는다.
     *          동작의 저작(어휘 · 칸)은 그대로이고 이것은 실행 기록만이다 — 언리얼 Gameplay Debugger 의 행동 기록 · Visual Logger 와 같은 자리.
     */
    struct SW_API ActionTrace
    {
        static constexpr uint32 kCapacity = 64;

        static void setEnabled( bool bEnabled );
        static bool isEnabled();
        /** @brief 호출 하나를 적습니다(`GameActionCall::invoke` 가 켜졌을 때만 부른다). */
        static void record( const ActionEntry& entry, const ActionContext& context, ActionResult result );
        /** @brief 최근 호출(오래된 것부터)을 @p outListEntry 에 채웁니다(먼저 비운다). */
        static void collect( vector<ActionTraceEntry>& outListEntry );
        /** @brief 기록을 비웁니다(켜고 끄기는 그대로). */
        static void clear();
    };

    /** @brief `SW_GAME_ACTION` 이 두는 정적 등록자입니다. */
    SW_DEFINE_NAMED_ENTRY_REGISTRAR( ActionRegistrar, ActionEntry );
} // namespace sw

/**
 * @brief 동작을 그 함수의 .cpp 에서 등록합니다(`namespace sw` 안, 파일 범위).
 * @param varName   파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pName     데이터가 적는 이름(리터럴, `<영역>.<동작>`)
 * @param TParams   칸 구조체. 없으면 `VocabularyNoParams`
 * @param pFunction `ActionResult( ActionContext&, const TParams& )` 함수의 주소
 */
#define SW_GAME_ACTION( varName, pName, TParams, pFunction ) \
    SW_STATIC_REGISTRATION( gameAction, ::sw::ActionEntry, ::sw::ActionRegistrar, varName, pName, &::sw::InstancedStructOps::of<TParams>(), &::sw::ActionThunk<TParams, pFunction>::call )
