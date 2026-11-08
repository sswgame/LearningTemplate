/**
 * @file EditorAssetIndex.h
 * @brief Resource 트리를 애셋 종류 등록로 분류합니다 — 퀵 런처 항목과 리소스 카탈로그 개수(ImGui 없음).
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndexEntry.h"

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorAssetIndex
     * @brief 파일 하나의 종류는 애셋 종류 등록으로 정합니다(`EditorAssetTypeRegistry::findKindInfoForPath`). 퀵 런처 · 카탈로그가 분류를 따로 적지 않습니다.
     */
    class EditorAssetIndex
    {
    public:
        /** @brief 파일 하나를 퀵 런처 항목으로 만듭니다. 리소스 트리 밖이거나 알려진 종류가 아니면 false 이고 항목은 쓰지 않습니다. */
        [[nodiscard]] static bool classifyFile( const AssetPaths& assetPaths, string_view filePath, EditorAssetIndexEntry& outEntry );
        /** @brief Resource 트리 전체를 분류해 채웁니다. 워커(`EditorAssetIndexJob`)에서도 돌아 경로 상태 @p assetPaths 는 부른 쪽이 넘긴다. */
        static void collectEntries( const AssetPaths& assetPaths, vector<EditorAssetIndexEntry>& outList, const StopToken& stopToken = {} );
        /** @brief 파일 목록을 종류별로 셉니다. 등록된 모든 종류가 한 줄씩 나옵니다(0 개 포함). */
        static void countFiles( const vector<string>& listFilePath, EditorResourceCatalogCounts& outCounts );
        /** @brief Resource 트리 전체를 종류별로 셉니다. */
        static void collectCatalogCounts( const AssetPaths& assetPaths, EditorResourceCatalogCounts& outCounts, const StopToken& stopToken = {} );
    };
} // namespace sw::editor
