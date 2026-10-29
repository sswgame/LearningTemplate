#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Asset/Texture/TextureImporter.h"
#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Graphics/Texture/Texture2D.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct TextureAssetTypeActionsInternal
        {
            static constexpr Color kCheckerDark{ Color::fromBytes( 38, 40, 46, 255 ) };
            static constexpr Color kCheckerLight{ Color::fromBytes( 58, 62, 70, 255 ) };
            static constexpr Color kFrame{ Color::fromBytes( 255, 255, 255, 200 ) };
            static constexpr Color kSun{ Color::fromBytes( 240, 200, 80, 230 ) };
            static constexpr Color kHill{ Color::fromBytes( 70, 180, 120, 230 ) };
        };

        /** @brief 텍스처 — 썸네일은 그 텍스처 자체이고, 읽기 전에는 체크무늬 위 액자다. 뷰포트에 끌어 놓으면 그 텍스처의 스프라이트를 만든다. */
        class TextureAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool hasImagePreview() const override { return true; }

            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width   = maxPos._x - minPos._x;
                const float32 height  = maxPos._y - minPos._y;
                const float32 centerX = minPos._x + width * 0.5f;
                const float32 centerY = minPos._y + height * 0.5f;

                // 체크무늬 배경. 격자는 정수 인덱스로 돈다 — 위치는 곱셈 한 번, 셀 색은 인덱스 합의 홀짝이다.
                constexpr float32 kCheckerSize = 6.0f;
                const float32     startX       = minPos._x + 4.0f;
                const float32     startY       = minPos._y + 4.0f;
                const int32       countX       = static_cast<int32>( MathUtil::max( ( maxPos._x - 4.0f - startX ) / kCheckerSize, 0.0f ) ) + 1;
                const int32       countY       = static_cast<int32>( MathUtil::max( ( maxPos._y - 4.0f - startY ) / kCheckerSize, 0.0f ) ) + 1;
                for ( int32 indexY = 0; indexY < countY; ++indexY )
                {
                    const float32 y = startY + static_cast<float32>( indexY ) * kCheckerSize;
                    if ( y >= maxPos._y - 4.0f )
                        break;

                    for ( int32 indexX = 0; indexX < countX; ++indexX )
                    {
                        const float32 x = startX + static_cast<float32>( indexX ) * kCheckerSize;
                        if ( x >= maxPos._x - 4.0f )
                            break;

                        const bool bDark = ( ( indexX + indexY ) % 2 ) == 0;
                        pDrawList->AddRectFilled( ImVec2( x, y ), ImVec2( x + kCheckerSize, y + kCheckerSize ),
                                                  bDark ? EditorColorUtil::toImU32( TextureAssetTypeActionsInternal::kCheckerDark ) : EditorColorUtil::toImU32( TextureAssetTypeActionsInternal::kCheckerLight ) );
                    }
                }
                // 액자 테두리 · 해 · 산
                pDrawList->AddRect( ImVec2( minPos._x + width * 0.16f, minPos._y + height * 0.16f ), ImVec2( minPos._x + width * 0.84f, minPos._y + height * 0.84f ),
                                    EditorColorUtil::toImU32( TextureAssetTypeActionsInternal::kFrame ), 2.0f );
                pDrawList->AddCircleFilled( ImVec2( centerX + width * 0.15f, centerY - height * 0.12f ), width * 0.08f, EditorColorUtil::toImU32( TextureAssetTypeActionsInternal::kSun ) );
                ImVec2 arrTriangle[3] = { ImVec2( centerX - width * 0.22f, centerY + height * 0.22f ), ImVec2( centerX, centerY - height * 0.05f ), ImVec2( centerX + width * 0.22f, centerY + height * 0.22f ) };
                pDrawList->AddConvexPolyFilled( arrTriangle, 3, EditorColorUtil::toImU32( TextureAssetTypeActionsInternal::kHill ) );
                return true;
            }

            virtual bool dropInViewport( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos ) const override
            {
                (void)EditorAssetCommands::spawnSprite( pManager, pPath, spawnPos ); // 실패는 spawnSprite 가 알린다
                return true;
            }
        };

        const TextureAssetTypeActions s_textureActions{};

        /** @brief 저작 원본 이미지의 확장자입니다. 엔진은 원본을 읽지 않으므로 엔진 정의에 없다 — 임포트하는 것이 리로드다. */
        constexpr string_view kArrTextureSourceExt[] = { ".png", ".jpg", ".jpeg", ".tga", ".hdr", ".bmp" };

        const EditorAssetMatchRule kArrTextureRule[] = {
            EditorAssetMatchRule::suffixes( EditorAssetMatchMode::Extension, kArrTextureSourceExt, SW_COUNT_OF( kArrTextureSourceExt ), EditorAssetMatchPriority::Suffix,
                                            { "Texture", &TextureImporter::importChangedSourceImage, AssetImportThread::Worker } ),
            EditorAssetMatchRule::engineType( Texture2D::kAssetType, { "Texture" } ),
        };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Texture, Texture2D::kAssetType._name.data(), 300, "Texture", "Textures", nullptr, editoricon::kTexture, { 0.35f, 0.85f, 0.45f, 1.0f }, false, true, true,
                          kArrTextureRule, SW_COUNT_OF( kArrTextureRule ), nullptr, &s_textureActions );
} // namespace sw::editor
