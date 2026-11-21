/**
 * @file InspectorInvokeResult.h
 * @brief 인스펙터가 마지막으로 부른 FUNCTION 의 결과입니다 — 실패 갈래(`ReflectCallResult`), 부른 대상 수와 실패 수, 첫 성공 대상의 반환값 글.
 * @details 다중 선택은 같은 메서드를 대상마다 부르므로 결과가 대상 수만큼 온다. 패널은 한 줄(성공이면 반환값, 실패면 첫 실패 갈래와 실패 수/대상 수)만 보인다.
 *          언리얼 Details 의 CallInEditor 는 결과를 보이지 않는다 — 우리는 틀린 인자 · 없는 함수를 삼키지 않으려고 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionInvoke.h"

namespace sw::editor
{
    /** @brief 마지막 FUNCTION 호출(대상 하나 이상)의 결과입니다. `begin` 으로 비우고 대상마다 `record` 합니다. */
    struct InspectorInvokeResult
    {
        string            _methodName;                            ///< 부른 메서드 이름. 비면 아직 부르지 않았다
        string            _returnText;                            ///< 첫 성공 대상의 반환값 글(void 면 빈 글)
        ReflectCallResult _firstFailure{ ReflectCallResult::Ok }; ///< 처음 실패한 대상의 갈래. 모두 성공이면 Ok
        uint32            _targetCount{ 0 };                      ///< 부른 대상 수(그 사이 사라진 대상은 세지 않는다)
        uint32            _failureCount{ 0 };                     ///< 그중 실패한 대상 수

        /** @brief 메서드 @p methodName 의 새 호출로 비웁니다. */
        void begin( string_view methodName )
        {
            _methodName   = string{ methodName };
            _returnText   = {};
            _firstFailure = ReflectCallResult::Ok;
            _targetCount  = 0;
            _failureCount = 0;
        }

        /** @brief 대상 하나의 호출 결과를 더합니다. 반환값 글은 첫 성공 대상의 것만 둔다(다중 선택의 값은 대상마다 다를 수 있다). */
        void record( ReflectCallResult result, const ReflectedValue& returnValue )
        {
            const bool bFirstSuccess = result == ReflectCallResult::Ok && _targetCount == _failureCount;
            ++_targetCount;
            if ( result != ReflectCallResult::Ok )
            {
                if ( _failureCount == 0 )
                    _firstFailure = result;
                ++_failureCount;
                return;
            }
            if ( bFirstSuccess && returnValue.isEmpty() == false )
                _returnText = returnValue.toText();
        }

        /** @brief 실패한 대상이 하나라도 있으면 true 입니다. */
        bool hasFailure() const { return _failureCount != 0; }
    };
} // namespace sw::editor
