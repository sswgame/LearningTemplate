/**
 * @file UIBindingConverterRegistry.h
 * @brief 바인딩 변환기(`{bind:필드, converter=Percent}`)의 이름 → 함수 레지스트리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Binding/UIBindingValue.h"

namespace sw
{
    /** @brief 변환기 함수입니다. @p in 을 바꿔 @p outValue 에 둡니다. 바꿀 수 없으면 false 를 반환하고 위젯 필드는 그대로 둡니다. */
    using UIBindingConvertFunction = bool ( * )( const UIBindingValue& in, UIBindingValue& outValue );

    /** @struct UIBindingConverter @brief 변환기 하나입니다. 받는 종류, 내는 종류, 함수를 가집니다(종류는 바인딩을 연결할 때 타입 검사에 씁니다). */
    struct UIBindingConverter
    {
        hashed_string            _name{};
        UIBindingConvertFunction _pConvert{ nullptr };
        UIBindingValueKind       _inputKind{ UIBindingValueKind::None };
        UIBindingValueKind       _outputKind{ UIBindingValueKind::None };
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIBindingConverterRegistry
     * @brief 이름으로 찾는 변환기 레지스트리입니다(`UISystem::getBindingConverters`). 엔진 기본 변환기는 `Percent`(0..1 → "75%"), `Invert`(불리언 뒤집기),
     *        `NotEmpty`(텍스트가 비지 않았는지), `IsZero`(숫자가 0 인지), `Seconds`(초 → "m:ss")입니다.
     * @details 게임과 키트가 자기 변환기를 추가한다. 함수가 모듈 이미지 안에 있으면 그 모듈이 언로드될 때 `UISystem` 이 제거한다(`removeCodeWithin`). 게임 스레드에서만 쓴다.
     */
    class SW_API UIBindingConverterRegistry
    {
    public:
        UIBindingConverterRegistry();

        /** @brief 엔진 기본 변환기를 올립니다(생성자가 부른다). */
        void registerEngineConverters();
        /** @brief 변환기를 등록합니다. 같은 이름이 있으면 교체합니다(게임과 키트가 엔진 기본 변환기를 같은 이름으로 덮을 수 있습니다). */
        void registerConverter( const UIBindingConverter& converter );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        const UIBindingConverter* findConverter( const hashed_string& name ) const;
        /** @brief 함수가 [@p pBegin, @p pEnd) 안에 있는 변환기를 제거합니다(모듈 핫 리로드). 제거한 개수를 반환합니다. */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );

    private:
        NameRegistry<UIBindingConverter> _registry;
    };
} // namespace sw
