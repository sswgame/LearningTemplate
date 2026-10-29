/**
 * @file InputMapXMLUtil.h
 * @brief 맵 정의(`InputMapSerialization.cpp`)와 사용자 바인딩(`InputRemapSerializer.cpp`) 두 XML 형식이 함께 쓰는 속성 읽기 · 쓰기를 선언합니다.
 * @details 패드 번호 · 수정 키 마스크 · 스틱 배율 · 발화 시간 같은 칸은 두 형식이 같은 규칙으로 읽어야 한다. 한쪽만 고치면 다른 파일로 같은 깨진 값이 들어온다.
 *          두 번역 단위만 포함한다(엔진 안 헬퍼라 내보내지 않는다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    enum class GamepadStickAxis : uint8;

    struct float2;
    struct ActionBinding;
    struct InputSlot;

    class InputMap;
} // namespace sw

namespace sw
{
    struct InputMapXMLUtil
    {
        static constexpr int32 kMinTapCount = 2;   ///< MultiTap 은 두 번부터다
        static constexpr int32 kMaxTapCount = 255; ///< `ActionBinding::_tapCount` 가 uint8 이다

        /** @brief 적지 않은 속성의 기본값을 담은 바인딩입니다. 기본값의 원본은 `ActionBinding` 의 초기값 하나다. */
        static const ActionBinding& getDefaultBinding();

        /**
         * @brief 선택 속성 @p pName 을 읽어 있으면 @p inoutValue 에 넣습니다. 없으면 그대로입니다(부르는 쪽이 넣어 둔 초기값).
         * @details 값이 깨졌으면 `XMLNode` 가 원소 · 속성 이름으로 오류를 남기고 @p inoutbValid 를 내린다(값은 그대로). 폴백 인자를 받지 않는다.
         */
        template <typename T>
        static void readOptional( XMLNode node, const utf8* pName, T& inoutValue, bool& inoutbValid )
        {
            std::optional<T> value;
            if ( node.readOptionalAttribute( pName, value ) == false )
            {
                inoutbValid = false;
                return;
            }
            if ( value.has_value() )
                inoutValue = *value;
        }

        /** @brief 반드시 있어야 하는 속성 @p pName 을 읽습니다. 없거나 깨졌으면 `XMLNode` 가 오류를 남기고 @p inoutbValid 를 내립니다(값은 그대로). */
        template <typename T>
        static void readRequired( XMLNode node, const utf8* pName, T& outValue, bool& inoutbValid )
        {
            const std::optional<T> value = node.readRequiredAttribute<T>( pName );
            if ( value.has_value() )
                outValue = *value;
            else
                inoutbValid = false;
        }

        /**
         * @brief `pad` 속성을 슬롯 범위(0 ~ `kMaxGamepadSlot` - 1)에서 읽습니다. 없으면 0 번 패드입니다.
         * @return 정수가 아니거나 범위 밖이면 경고하고 false 를 반환하며, 호출한 쪽은 그 바인딩을 버립니다.
         * @details 값을 잘라서 담으면 "256" 이 0 번, "-1" 이 255 번 패드가 되고, 4 번 이상은 없는 패드에 말없이 묶인다.
         *          패드 번호를 읽는 세 곳(리소스 스틱, 유저 스틱, 유저 단일 버튼)이 모두 이 함수를 거친다.
         */
        [[nodiscard]] static bool tryGetPadIndex( XMLNode node, uint8& outPadIndex );
        /**
         * @brief `modifierMask` 속성을 아는 수정 키 비트(`ModifierKey::All`) 범위에서 읽습니다. 없으면 0(수정 키 없음)입니다.
         * @return 정수가 아니거나 모르는 비트가 있으면 경고하고 false 를 반환하며, 호출한 쪽은 그 바인딩을 버립니다. 범위를 검사하지 않으면 "257" 이 Ctrl 로 읽힙니다.
         */
        [[nodiscard]] static bool tryGetModifierMask( XMLNode node, uint8& outModifierMask );
        /**
         * @brief 1D 축 합성의 두 슬롯을 읽습니다. `source="gamepad"`(와 `pad`)면 게임패드 버튼 이름, 없거나 `key` 면 키 이름을 읽습니다.
         * @return 이름을 모르거나 패드 번호가 틀리면 경고하고 false 를 반환하며, 호출한 쪽은 그 바인딩을 버립니다.
         */
        [[nodiscard]] static bool tryReadAxisSlots( XMLNode node, const utf8* pNegativeName, const utf8* pPositiveName, InputSlot& outNegative, InputSlot& outPositive );

        /** @brief 스틱 배율 · 프레임 시간 곱 · 성분(`scaleX` · `scaleY` · `scaleByDelta` · `axis`)을 적습니다. 기본값(1, 1, false, 둘 다)이면 적지 않아 기존 파일이 그대로 나온다. */
        static void writeStickModifier( XMLNode node, const ActionBinding& binding );
        /**
         * @brief 스틱 배율 · 프레임 시간 곱 · 성분을 읽습니다. 없는 속성은 `ActionBinding` 의 초기값(1, 1, false, 둘 다)입니다.
         * @return `axis` 가 `X` · `Y` 가 아닌 글이면 오류를 남기고 @p inoutbValid 를 내린 뒤 false 입니다(그 바인딩은 버린다 — 2D 로 바꿔 묶으면
         *         1D 액션이 엉뚱한 성분을 읽는다).
         */
        [[nodiscard]] static bool readStickModifier( XMLNode node, float2& outAxisScale, bool& outScaleByDelta, GamepadStickAxis& outAxis, bool& inoutbValid );

        /** @brief 바인딩마다 발화 시간 · 부호 뒤집기(`time` · `negate`)를 적습니다. 기본값(0, false)이면 적지 않아 기존 파일이 그대로 나온다. */
        static void writeBindingModifier( XMLNode node, const ActionBinding& binding );
        /**
         * @brief @p node 를 읽어 방금 더한 바인딩(인덱스 @p previousCount)에 발화 시간 · 부호 뒤집기를 겁니다.
         * @details 바인딩을 더하지 못했으면(모르는 키 등) 아무것도 하지 않는다. 없는 속성은 `ActionBinding` 의 초기값(0 = 맵 값, false, 2 번)이다.
         *          깨진 값이나 2~255 밖의 탭 수는 자르지 않고 오류로 @p inoutbValid 를 내린다.
         */
        static void readBindingModifier( InputMap& inputMap, const hashed_string& action, uint32 previousCount, XMLNode node, bool& inoutbValid );
    };
} // namespace sw
