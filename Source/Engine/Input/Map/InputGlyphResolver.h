/**
 * @file InputGlyphResolver.h
 * @brief 액션 · 슬롯을 UI 프롬프트 문자열("[ E ]", PlayStation 이면 "[ Circle ]")로 바꾸는 글리프 조회를 선언합니다.
 * @details 맵의 공개 읽기 API(`InputMap::getBinding`)만 쓰는 상태 없는 조회라서 맵이 들고 있지 않다.
 *          언리얼 CommonInput 이 Enhanced Input 매핑과 따로 플랫폼별 키 아이콘을 고르는 것과 같은 나눔이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    enum class InputGlyphStyle : uint8;

    struct InputSlot;

    class InputMap;
} // namespace sw

namespace sw
{
    /**
     * @struct InputGlyphResolver
     * @brief 바인딩을 장치별 표기로 바꿉니다. 액션의 첫 바인딩 중 그 장치로 보일 것이 있는 것을 고릅니다.
     */
    struct SW_API InputGlyphResolver
    {
        /** @brief 맵을 들고 있는 `InputManager` 의 지금 활성 장치(없으면 키보드 · 마우스)로 액션의 표기를 반환합니다. 보일 바인딩이 없으면 "[ ? ]" 입니다. */
        static string findActionGlyph( const InputMap& map, const hashed_string& action );
        /** @brief 실제 장치와 상관없이 @p style 의 표기를 반환합니다(에디터 Glyph Previewer 가 플랫폼별 표기를 나란히 비교한다). */
        static string findActionGlyph( const InputMap& map, const hashed_string& action, InputGlyphStyle style );
        /** @brief 슬롯 하나의 표기(`[ Space ]`)를 반환합니다. 리바인딩 화면이 아직 적용하지 않은 키를 보일 때 씁니다. */
        static string makeSlotGlyph( const InputSlot& slot, InputGlyphStyle style );
    };
} // namespace sw
