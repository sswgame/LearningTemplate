/**
 * @file UiBindingConverterRegistry.h
 * @brief 바인딩 변환기(`{bind:필드, converter=Percent}`)의 이름 → 함수 등록부입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Binding/UiBindingValue.h"

namespace sw
{
    /** @brief 변환기 함수입니다 — @p in 을 바꿔 @p outValue 에 둡니다. 바꿀 수 없으면 false(위젯 칸은 그대로)입니다. */
    using UiBindingConvertFunction = bool ( * )( const UiBindingValue& in, UiBindingValue& outValue );

    /** @struct UiBindingConverter @brief 변환기 하나 — 받는 갈래 · 내는 갈래 · 함수입니다(갈래는 바인딩을 걸 때 타입 검사에 쓴다). */
    struct UiBindingConverter
    {
        hashed_string            _name{};
        UiBindingConvertFunction _pConvert{ nullptr };
        UiBindingValueKind       _inputKind{ UiBindingValueKind::None };
        UiBindingValueKind       _outputKind{ UiBindingValueKind::None };
    };
} // namespace sw

namespace sw
{
    /**
     * @class UiBindingConverterRegistry
     * @brief 이름으로 찾는 변환기 표입니다(`UiSystem::getBindingConverters`). 엔진 기본: `Percent`(0..1 → "75%") · `Invert`(불리언 뒤집기) ·
     *        `NotEmpty`(글 → 비지 않았나) · `IsZero`(숫자 → 0 인가) · `Seconds`(초 → "m:ss").
     * @details 게임 · 키트가 자기 변환기를 더합니다. 함수가 모듈 이미지 안이면 그 모듈이 내려갈 때 `UiSystem` 이 걷어 냅니다(`removeCodeWithin`). 게임 스레드만.
     */
    class SW_API UiBindingConverterRegistry
    {
    public:
        UiBindingConverterRegistry();

        /** @brief 엔진 기본 변환기를 올립니다(생성자가 부른다). */
        void registerEngineConverters();
        /** @brief 변환기를 올립니다. 같은 이름이 있으면 바꿉니다. */
        void registerConverter( const UiBindingConverter& converter );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        const UiBindingConverter* findConverter( const hashed_string& name ) const;
        /** @brief 함수가 [@p pBegin, @p pEnd) 안인 변환기를 뗍니다(모듈 핫 리로드). 뗀 수를 반환합니다. */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );

    private:
        vector<UiBindingConverter> _listConverter;
    };
} // namespace sw
