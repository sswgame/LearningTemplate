/**
 * @file ScopedGlobalVariableValue.h
 * @brief 케이스가 전역 변수(`gv_*`)를 바꾸고, 스코프를 나갈 때 앞 값으로 되돌리는 RAII 가드입니다.
 * @details 손으로 "앞 값 저장 → 바꿈 → 되돌림" 을 쓰면 사이의 `SW_REQUIRE_*` 가 케이스를 끊을 때 되돌림이 건너뛰어져, 같은 프로세스의 뒤 케이스가
 *          바뀐 값을 보고 순서 · 샤드에 따라 결과가 갈린다. 되돌림을 소멸자에 두면 어떤 길로 나가도 돈다(언리얼 자동화의 콘솔 변수 복원 ·
 *          유니티 `[TearDown]` 과 같은 자리). 값은 매니저와 같은 글 형식으로 주고받아 변경 콜백도 실제 길과 같이 불린다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

namespace sw::test
{
    /**
     * @class ScopedGlobalVariableValue
     * @brief 살아 있는 동안 변수 하나를 바꿔 두고, 사라질 때 만들 때의 값으로 되돌립니다.
     */
    class ScopedGlobalVariableValue
    {
    public:
        /** @brief @p info 의 지금 값을 기억하고 @p value 로 바꿉니다. 바뀌었는지는 부르는 쪽이 `isApplied` 로 단언한다. */
        ScopedGlobalVariableValue( GlobalVariableInfo& info, string_view value )
            : _pInfo( &info ), _previous( info.getValueAsString() ), _bApplied( info.setValueFromString( value ) ? SW_TRUE : SW_FALSE )
        {
        }
        ~ScopedGlobalVariableValue() { (void)_pInfo->setValueFromString( _previous ); }
        ScopedGlobalVariableValue( const ScopedGlobalVariableValue& )            = delete;
        ScopedGlobalVariableValue& operator=( const ScopedGlobalVariableValue& ) = delete;

        /** @brief 만들 때 준 값으로 바뀌었으면 true 입니다. */
        [[nodiscard]] bool isApplied() const { return _bApplied != SW_FALSE; }
        /** @brief 같은 변수를 다른 값으로 다시 바꿉니다. 되돌릴 값은 만들 때의 것 그대로입니다. */
        [[nodiscard]] bool set( string_view value ) { return _pInfo->setValueFromString( value ); }

    private:
        GlobalVariableInfo* _pInfo;        ///< 바꾼 변수. 매니저가 등록을 풀 때까지 산다
        string              _previous;     ///< 만들 때의 값(글) — 소멸자가 되돌린다
        uint8               _bApplied : 1; ///< 만들 때 준 값으로 바뀌었는가
    };
} // namespace sw::test
