#include "pch.h"

#include "Editor/AssetActions/EditorAssetThumbnailRenderer.h"
#include "Editor/Common/Asset/Texture/TextureImporter.h"
#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Graphics/Texture/Texture2D.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 텍스처 — 썸네일은 그 텍스처 자체이고, 읽기 전에는 체크무늬 위 액자다. 뷰포트에 끌어 놓으면 그 텍스처의 스프라이트를 만든다. */
        class TextureAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool hasImagePreview() const override { return true; }

            /** @brief 원본 이미지는 임포트한 DDS 를, 엔진 텍스처는 그 자신을 미리 본다. 산출물 자리가 없는 원본(`textures_raw/` 밖)은 미리보기가 없다. */
            virtual string findImagePreviewPath( string_view path ) const override
            {
                return FileUtil::getExtension( path ) == ".dds" ? string( path ) : TextureImporter::makeImportedTexturePath( path );
            }

            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                return EditorAssetThumbnailRenderer::drawTexture( pDrawList, minPos, maxPos );
            }

            [[nodiscard]] virtual bool canPlaceInViewport() const override { return true; }

            virtual void placeInViewport( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose ) const override
            {
                (void)EditorAssetCommands::spawnSprite( pManager, pPath, pose ); // 실패는 spawnSprite 가 알린다
            }
        };

        const TextureAssetTypeActions s_textureActions{};

        /** @brief 저작 원본 이미지의 확장자입니다. 엔진은 원본을 읽지 않으므로 엔진 정의에 없다 — 임포트하는 것이 리로드다. */
        constexpr string_view kArrTextureSourceExt[] = { ".png", ".jpg", ".jpeg", ".tga", ".hdr", ".bmp" };

        const EditorAssetMatchRule kArrTextureRule[] = {
            EditorAssetMatchRule::suffixes( EditorAssetMatchMode::Extension, kArrTextureSourceExt, SW_COUNT_OF( kArrTextureSourceExt ), EditorAssetMatchPriority::Suffix,
                                            { "Texture", &TextureImporter::importChangedSourceImage, AssetImportThread::WorkerParallel } ),
            EditorAssetMatchRule::engineType( Texture2D::kAssetType, { "Texture" } ),
        };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Texture, Texture2D::kAssetType._name.data(), 300, "Texture", "Textures", nullptr, SW_EDITOR_ICON_NAME( "Texture" ), SW_THEME_COLOR_KEY( "Asset", "Texture" ), true, true,
                          kArrTextureRule, SW_COUNT_OF( kArrTextureRule ), nullptr, &s_textureActions );
} // namespace sw::editor
