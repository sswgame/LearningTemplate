#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Editor/AssetActions/EditorAssetTypeActions.h"
#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 텍스처 — 썸네일은 그 텍스처 자체이고, 읽기 전에는 체크무늬 위 액자다. 뷰포트에 끌어 놓으면 그 텍스처의 스프라이트를 만든다. */
        class TextureAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual EditorAssetType getKind() const override { return EditorAssetType::Texture; }

            virtual bool hasImagePreview() const override { return true; }

            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width   = maxPos._x - minPos._x;
                const float32 height  = maxPos._y - minPos._y;
                const float32 centerX = minPos._x + width * 0.5f;
                const float32 centerY = minPos._y + height * 0.5f;

                // 체크무늬 배경. 격자는 정수 인덱스로 돈다 — 위치는 곱셈 한 번, 칸 색은 인덱스 합의 홀짝이다.
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
                                                  bDark ? IM_COL32( 38, 40, 46, 255 ) : IM_COL32( 58, 62, 70, 255 ) );
                    }
                }
                // 액자 테두리 · 해 · 산
                pDrawList->AddRect( ImVec2( minPos._x + width * 0.16f, minPos._y + height * 0.16f ), ImVec2( minPos._x + width * 0.84f, minPos._y + height * 0.84f ),
                                    IM_COL32( 255, 255, 255, 200 ), 2.0f );
                pDrawList->AddCircleFilled( ImVec2( centerX + width * 0.15f, centerY - height * 0.12f ), width * 0.08f, IM_COL32( 240, 200, 80, 230 ) );
                ImVec2 arrTriangle[3] = { ImVec2( centerX - width * 0.22f, centerY + height * 0.22f ), ImVec2( centerX, centerY - height * 0.05f ), ImVec2( centerX + width * 0.22f, centerY + height * 0.22f ) };
                pDrawList->AddConvexPolyFilled( arrTriangle, 3, IM_COL32( 70, 180, 120, 230 ) );
                return true;
            }

            virtual bool dropInViewport( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos ) const override
            {
                (void)EditorAssetCommands::spawnSprite( pManager, pPath, spawnPos ); // 실패는 spawnSprite 가 알린다
                return true;
            }
        };

        const EditorAssetTypeActionsRegistrar<TextureAssetTypeActions> s_textureAssetTypeActionsRegistrar{};
    } // namespace
} // namespace sw::editor
