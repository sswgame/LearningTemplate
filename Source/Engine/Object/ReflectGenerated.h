/**
 * @file ReflectGenerated.h
 * @brief ReflectionParser 가 만드는 .gen.cpp 와 builtins 생성 코드가 맨 앞에 include 하는 공통 머리 헤더입니다.
 * @details 생성 코드가 쓰는 헤더는 여기에서만 더하고 뺍니다. FileHeader.tpl / BuiltinFileHeader.tpl 은 이 파일을 include 합니다.
 *          생성 TU 전부가 이 파일을 읽으므로 생성 코드(템플릿 · CodeGenerator)가 실제로 쓰는 이름의 헤더만 둡니다. 원본 헤더가 쓰는
 *          이름은 원본 헤더가 직접 include 합니다(`GameObjectManager.h` 를 두면 생성 TU 마다 헤더 약 24 개를 더 읽습니다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Foundation/Foundation.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionMetadataText.h"
#include "Engine/Reflection/ReflectionUnits.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Serialization/Fixed/FixedFieldDesc.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

namespace sw
{
    class StateIO;
} // namespace sw

namespace sw::generated
{
    /**
     * @brief 형식에 `serializeState( StateIO& )` 가 있는지입니다 — 생성 코드가 `TypeMetadata::_bOverridesSerializeState` 에 대입한다(생성기가 이름 · 인자 수로 내리던 판정).
     * @details `StateIO` 는 불완전 형식으로 충분하다(참조로만 넘긴다) — 생성 단위마다 `StateIO.h` 를 끌지 않는다. 물려받은 것도 센다.
     */
    template <typename T, typename = void>
    struct HasSerializeState : std::false_type
    {
    };

    template <typename T>
    struct HasSerializeState<T, std::void_t<decltype( std::declval<T&>().serializeState( std::declval<::sw::StateIO&>() ) )>> : std::true_type
    {
    };
} // namespace sw::generated
