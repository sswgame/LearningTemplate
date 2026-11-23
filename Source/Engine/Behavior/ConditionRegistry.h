/**
 * @file ConditionRegistry.h
 * @brief 조건의 등록 줄(`ConditionEntry`)과 이름 → 조건 표, 등록 매크로 `SW_CONDITION` 입니다. 식을 들기만 하는 쪽은 `ConditionExpr.h` 만 본다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Registry/NamedEntryRegistry.h"

#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Reflection/InstancedStruct.h"

namespace sw
{
    /** @brief 조건 이름 하나의 등록 줄입니다. 글자는 등록한 이미지의 정적 저장소에 있습니다. */
    struct ConditionEntry
    {
        static constexpr const utf8* kEntryKind = "Condition";

        const utf8*               _pName;
        const InstancedStructOps* _pArgsOps;
        ConditionFunction         _function;

        const utf8* getEntryName() const { return _pName; }
        /** @brief 등록 목록입니다(Engine 이미지의 함수 정적 — 모듈 경로 없이 도는 테스트 실행 파일에서도 보이고, 교체 이미지의 같은 이름은 대체한다 `RegistrationReloadScope`). 이름은 대소문자를 본다. */
        SW_API static LockedRegistrationList<const ConditionEntry>& getEntries();
    };

    /** @brief `float64( const GameplayContext&, const TArgs& )`(참거짓 반환도 된다) 함수를 형식을 지운 함수로 옮깁니다. */
    template <typename TArgs, auto pFunction>
    struct ConditionThunk
    {
        static float64 call( const GameplayContext& context, const void* pArgs )
        {
            return static_cast<float64>( pFunction( context, *static_cast<const TArgs*>( pArgs ) ) );
        }
    };

    /** @brief 이름 → 조건 표입니다(대소문자 구분). 프레임마다 도는 경로가 아니다(읽을 때만 찾는다). */
    using ConditionRegistry = NamedEntryRegistry<ConditionEntry>;

    /** @brief `SW_CONDITION` 이 두는 정적 등록자입니다. */
    SW_DEFINE_NAMED_ENTRY_REGISTRAR( ConditionRegistrar, ConditionEntry );
} // namespace sw

/**
 * @brief 조건을 그 함수의 .cpp 에서 등록합니다(`namespace sw` 안, 파일 범위). 이름이 곧 데이터가 적는 이름입니다.
 * @param name      식별자 이름(`weather`)
 * @param TArgs     칸 구조체(`ConditionNameListArgs`). 없으면 `VocabularyNoParams`
 * @param pFunction `float64( const GameplayContext&, const TArgs& )` 함수의 주소(참거짓 반환도 된다)
 */
#define SW_CONDITION( name, TArgs, pFunction ) \
    SW_STATIC_REGISTRATION( condition, ::sw::ConditionEntry, ::sw::ConditionRegistrar, name, #name, &::sw::InstancedStructOps::of<TArgs>(), &::sw::ConditionThunk<TArgs, pFunction>::call )
