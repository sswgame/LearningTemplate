/**
 * @file EditorAssetCatalog.h
 * @brief Resource 트리를 애셋 종류 등록로 분류합니다 — 퀵 런처 항목과 리소스 카탈로그 개수(ImGui 없음).
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndexEntry.h"

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    class EditorAssetIndex;

    /**
     * @class EditorAssetCatalog
     * @brief 파일 하나의 종류는 애셋 종류 등록으로 정합니다(`EditorAssetTypeRegistry::findKindInfoForPath`). 퀵 런처 · 카탈로그가 분류를 따로 적지 않습니다.
     */
    class EditorAssetCatalog
    {
    public:
        /** @brief 파일 하나를 퀵 런처 항목으로 만듭니다. 리소스 트리 밖이거나 알려진 종류가 아니면 false 이고 항목은 쓰지 않습니다. */
        [[nodiscard]] static bool classifyFile( const AssetPaths& assetPaths, const AbsolutePath& filePath, EditorAssetIndexEntry& outEntry );
        /** @brief Resource 트리 전체를 분류해 채웁니다. 워커(`EditorAssetIndexJob`)에서도 돌아 경로 상태 @p assetPaths 는 부른 쪽이 넘긴다. */
        static void collectEntries( const AssetPaths& assetPaths, vector<EditorAssetIndexEntry>& outList, const StopToken& stopToken = {} );
        /** @brief 에디터의 에셋 색인(`EditorAssetIndex`, UI 스레드)이 아는 파일을 분류해 채웁니다 — 디스크를 걷지 않는다(선택 팝업 · Open 이 UI 스레드에서 부른다). */
        static void collectEntries( const AssetPaths& assetPaths, const EditorAssetIndex& index, vector<EditorAssetIndexEntry>& outList );
        /**
         * @brief 목록을 지을 에디터의 에셋 색인입니다(UI 스레드). 기동 뒤 첫 전체 스캔이 끝나기 전(수 초)이거나 에디터 컨텍스트가 없으면 nullptr 입니다.
         * @details 피커 · Open · Quick Open · 명령 팔레트 · 카탈로그 · 리소스 개수가 모두 이 규칙 하나를 쓴다 — 색인이 있으면 디스크를 걷지 않고, 첫 스캔 전에만 트리를 걷는다
         *          (그 사이 연 목록이 비어 보이지 않게).
         */
        [[nodiscard]] static const EditorAssetIndex* findReadyIndex();
        /** @brief `findReadyIndex` 가 있으면 그것으로, 없으면 트리를 걸어 채웁니다. UI 스레드에서만 부른다(선택 팝업 · Open). */
        static void collectEntriesOnUIThread( const AssetPaths& assetPaths, vector<EditorAssetIndexEntry>& outList );
        /** @brief 파일 목록을 종류별로 셉니다. 등록된 모든 종류가 한 줄씩 나옵니다(0 개 포함). */
        static void countFiles( const vector<string>& listFilePath, EditorResourceCatalogCounts& outCounts );
        /** @brief Resource 트리 전체를 종류별로 셉니다. */
        static void collectCatalogCounts( const AssetPaths& assetPaths, EditorResourceCatalogCounts& outCounts, const StopToken& stopToken = {} );
        /** @brief 에디터의 에셋 색인이 아는 파일을 종류별로 셉니다(디스크를 걷지 않는다). */
        static void collectCatalogCounts( const AssetPaths& assetPaths, const EditorAssetIndex& index, EditorResourceCatalogCounts& outCounts );
    };
} // namespace sw::editor
