/**
 * @file EditorAssetIndex.h
 * @brief Resource 트리를 애셋 종류 등록로 분류합니다 — 퀵 런처 항목과 리소스 카탈로그 개수(ImGui 없음).
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    /** @brief Resource 폴더 스캔으로 만든 퀵 런처용 파일 항목 */
    struct EditorAssetIndexEntry
    {
        EditorAssetType _kind{};   ///< 애셋 종류(`EditorAssetTypeRegistry::findKind`)
        string          _category; ///< 종류의 단수 이름(`EditorAssetTypeRegistration::_pDisplayName`)
        string          _title;
        string          _detail;
        string          _path; ///< 리소스 id(콘텐츠 브라우저 · 끌어 놓기와 같은 형태)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 리소스 카탈로그의 종류 하나 */
    struct EditorResourceCatalogCount
    {
        EditorAssetType _kind{};
        const utf8*     _pLabel{ nullptr }; ///< 종류의 브라우저 라벨(`EditorAssetTypeRegistration::_pBrowserLabel`)
        size_t          _count{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로파일러 리소스 카탈로그 — 종류 등록의 순서대로 종류마다 한 줄 */
    struct EditorResourceCatalogCounts
    {
        vector<EditorResourceCatalogCount> _listKindCount;
    };
} // namespace sw::editor

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
