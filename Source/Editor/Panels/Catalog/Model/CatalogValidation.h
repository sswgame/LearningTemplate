/**
 * @file CatalogValidation.h
 * @brief 카탈로그 파일 하나를 그 종류의 로더로 읽어 보고 로더가 남긴 경고 · 오류를 모읍니다 — 카탈로그 창의 Validate 와 맵 검사의 카탈로그 검사가 함께 씁니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct AssetTypeDef;
} // namespace sw

namespace sw::editor
{
    /** @brief 파일 머리말을 참고하세요. */
    struct CatalogValidation
    {
        /** @brief 리소스 @p assetID 를 종류 @p kind 의 로더로 읽습니다. 읽혔으면 true 이고, 로더가 남긴 경고 · 오류 줄을 @p outListWarning 에 담습니다(비우고 채운다). */
        [[nodiscard]] static bool validate( const AssetTypeDef& kind, string_view assetID, vector<string>& outListWarning );
    };
} // namespace sw::editor
