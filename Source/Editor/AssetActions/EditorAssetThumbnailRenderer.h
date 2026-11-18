/**
 * @file EditorAssetThumbnailRenderer.h
 * @brief 에셋 종류별 콘텐츠 브라우저 카드 썸네일 그리기입니다(MVVM View — `EditorAssetThumbnailRenderer.cpp` 만 ImGui 를 쓴다).
 */
#pragma once
#include "Core/Foundation/Types.h"

struct ImDrawList;

namespace sw
{
    struct float2;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorAssetThumbnailRenderer
     * @brief 종류의 동작(`AssetActions/Model/<종류>AssetTypeActions.cpp`)이 `drawThumbnail` 에서 부르는 그리기 함수입니다.
     * @details 언리얼은 썸네일 그리기(`UThumbnailRenderer`)를 에셋 동작(`FAssetTypeActions`)과 가른다. 여기서는 등록 줄을 하나로 두려고 동작 객체가 썸네일을 맡되,
     *          ImGui 그리기는 이 View 에만 두어 동작 · 등록 파일(Model)이 ImGui 를 모르게 한다. 배경과 테두리는 브라우저가 그린다. 모두 그렸으면 true 다.
     */
    class EditorAssetThumbnailRenderer
    {
    public:
        static bool drawAudio( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );
        static bool drawMaterial( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );
        static bool drawPrefab( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );
        static bool drawScene( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );
        static bool drawShader( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );
        /** @brief 텍스처를 아직 읽지 못했을 때의 체크무늬 위 액자입니다(읽으면 브라우저가 텍스처 자체를 그린다 — `hasImagePreview`). */
        static bool drawTexture( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );
    };
} // namespace sw::editor
