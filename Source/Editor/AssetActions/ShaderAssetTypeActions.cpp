#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ShaderAssetTypeActionsInternal
        {
            static constexpr Color kCornerFill{ Color::fromBytes( 240, 120, 50, 255 ) };
            static constexpr Color kCornerOutline{ Color::fromBytes( 255, 210, 140, 255 ) };
        };

        /** @brief 셰이더 — 다이아몬드(프리즘) 썸네일. */
        class ShaderAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width        = maxPos._x - minPos._x;
                const float32 centerX      = minPos._x + width * 0.5f;
                const float32 centerY      = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
                const float32 radius       = width * 0.24f;
                ImVec2        arrCorner[4] = { ImVec2( centerX, centerY - radius ), ImVec2( centerX + radius * 0.85f, centerY ), ImVec2( centerX, centerY + radius ),
                                               ImVec2( centerX - radius * 0.85f, centerY ) };
                pDrawList->AddConvexPolyFilled( arrCorner, 4, EditorColorUtil::toImU32( ShaderAssetTypeActionsInternal::kCornerFill ) );
                pDrawList->AddPolyline( arrCorner, 4, EditorColorUtil::toImU32( ShaderAssetTypeActionsInternal::kCornerOutline ), ImDrawFlags_Closed, 1.5f );
                return true;
            }
        };

        const ShaderAssetTypeActions s_shaderActions{};

        /**
         * @brief 저작 셰이더 소스입니다. 공유 헤더 `.hlsli` 도 소스다. 엔진은 HLSL 전용이므로 `.glsl` 등은 없다.
         * @details `.spv` 는 넣지 않는다. 그것은 쿠킹된 산출물이므로(`Content/common/shaders/bin/`, 엔진 종류 `ShaderBinary`) 셰이더 소스로 세면 콘텐츠 브라우저가 빌드 출력 수백 개를 애셋으로 보여 준다.
         */
        constexpr string_view kArrShaderExt[] = { ".hlsl", ".hlsli" };

        const EditorAssetMatchRule kArrShaderRule[] = {
            EditorAssetMatchRule::suffixes( EditorAssetMatchMode::Extension, kArrShaderExt, SW_COUNT_OF( kArrShaderExt ), EditorAssetMatchPriority::Suffix ),
        };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Shader, EditorAssetTypeIDs::kShader, 400, "Shader", "Shaders", nullptr, editoricon::kShader, { 0.95f, 0.45f, 0.35f, 1.0f }, false, true, true, kArrShaderRule,
                          SW_COUNT_OF( kArrShaderRule ), nullptr, &s_shaderActions );
} // namespace sw::editor
