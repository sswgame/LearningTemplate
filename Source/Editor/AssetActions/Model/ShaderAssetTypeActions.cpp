#include "pch.h"

#include "Editor/AssetActions/EditorAssetThumbnailRenderer.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorThemeKey.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Graphics/Shader/ShaderSourceAssetKind.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 셰이더 — 다이아몬드(프리즘) 썸네일. */
        class ShaderAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                return EditorAssetThumbnailRenderer::drawShader( pDrawList, minPos, maxPos );
            }
        };

        const ShaderAssetTypeActions s_shaderActions{};

        /**
         * @brief 저작 셰이더 소스입니다 — 원본(`ShaderSourceAsset`)과 include 조각(`ShaderIncludeAsset`) 두 엔진 종류의 접미사로 고른다. 엔진은 HLSL 전용이다.
         * @details 쿠킹된 바이너리(`Content/common/shaders/bin/`, 엔진 종류 `ShaderBinary`)는 넣지 않는다. 셰이더 소스로 세면 콘텐츠 브라우저가 빌드 출력 수백 개를 애셋으로 보여 준다.
         */
        const EditorAssetMatchRule kArrShaderRule[] = {
            EditorAssetMatchRule::engineType( ShaderSourceAsset::kAssetType ),
            EditorAssetMatchRule::engineType( ShaderIncludeAsset::kAssetType ),
        };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Shader, EditorAssetTypeIDs::kShader, 400, "Shader", "Shaders", nullptr, SW_EDITOR_ICON_NAME( "Shader" ), SW_THEME_COLOR_KEY( "Asset", "Shader" ), true, true, kArrShaderRule,
                          SW_COUNT_OF( kArrShaderRule ), nullptr, &s_shaderActions );
} // namespace sw::editor
