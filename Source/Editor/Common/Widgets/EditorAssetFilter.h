/**
 * @file EditorAssetFilter.h
 * @brief 에셋 슬롯의 확장자 필터 문자열(`".png,.jpg .tga"`)을 나누고 경로가 맞는지 봅니다. ImGui 가 없어 단위 테스트가 직접 컴파일합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @struct EditorAssetFilter
     * @brief 필터는 쉼표 또는 공백으로 나눈 확장자 목록입니다. 한 개짜리(`".hlsl"`)도 같은 규칙으로 읽습니다.
     */
    struct SW_EDITOR_API EditorAssetFilter
    {
        /** @brief 필터 문자열을 확장자 목록으로 나눕니다. 빈 조각은 버립니다. 뷰는 @p filter 를 가리킵니다. */
        static vector<string_view> splitExtensions( string_view filter );
        /** @brief 필터가 비었거나 @p path 가 목록의 확장자 하나와 맞으면(대소문자 무시) true 입니다. */
        static bool matches( string_view path, string_view filter );
    };
} // namespace sw::editor
