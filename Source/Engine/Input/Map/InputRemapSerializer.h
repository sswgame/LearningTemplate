/**
 * @file InputRemapSerializer.h
 * @brief 플레이어의 키 리매핑 파일(`<UserBindings>`)을 쓰고 읽는 직렬화를 선언합니다.
 * @details 맵 정의(`<InputMap>`, `InputMap::loadFromAsset` · `saveToAsset`)는 디자인 타임 데이터이고, 이 파일은 플레이어가 디스크에 남기는 런타임 데이터라서 수명과 위치가 다르다.
 *          언리얼 `UEnhancedInputUserSettings` 가 매핑 컨텍스트와 따로 플레이어 키 프로필을 저장하는 것과 같은 나눔이다.
 *          수정 키 이름(`Ctrl+Shift`)은 두 형식과 디버그 · 에디터 재배치 파일이 함께 쓰는 글자라 여기 하나에 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class InputMap;
} // namespace sw

namespace sw
{
    /**
     * @struct InputRemapSerializer
     * @brief 맵의 현재 바인딩 전체를 사용자 바인딩 XML 로 쓰고, 그 파일을 읽어 맵에 묶습니다.
     */
    struct SW_API InputRemapSerializer
    {
        /** @brief 맵의 모든 액션 · 바인딩을 @p filePath 에 씁니다. 경로가 비었거나 쓰지 못하면 false 입니다. */
        [[nodiscard]] static bool save( const InputMap& map, string_view filePath );
        /**
         * @brief @p filePath 의 사용자 바인딩을 읽어 @p map 에 묶습니다.
         * @return 파일이 없거나 깨진 칸이 있거나, 가상 커서가 마우스로 바꾸는 스틱 · 패드 버튼을 다시 묶은 바인딩이 있으면 false 입니다.
         *         그 바인딩은 넣지 않는다(맵 읽기와 같은 거절).
         */
        [[nodiscard]] static bool load( InputMap& map, string_view filePath );

        /** @brief `modifier` 속성 글자(`Ctrl`, `Ctrl+Shift` …)를 수정 키 마스크로 읽습니다. 모르는 이름 · 빈 글 · 같은 이름 두 번이면 false 입니다. */
        [[nodiscard]] static bool tryParseModifierNames( string_view text, uint8& outModifierMask );
        /** @brief 수정 키 마스크를 `modifier` 속성 글자로 씁니다(`Ctrl+Shift`). 0 이면 빈 글입니다. */
        static string formatModifierNames( uint8 modifierMask );
    };
} // namespace sw
