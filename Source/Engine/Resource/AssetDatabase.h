/**
 * @file AssetDatabase.h
 * @brief 경로 ↔ GUID 레지스트리 (.meta 사이드카, 비-리플렉션 콘텐츠)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Uuid/Uuid.h"

namespace sw
{
    /**
     * @class AssetDatabase
     * @brief 메모리 상 에셋 식별. 사이드카는 `relativePath + ".meta"`
     */
    class SW_API AssetDatabase
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 경로 · 사이드카
        // ------------------------------------------------------------------------------
        /** @brief 빈 데이터베이스. */
        AssetDatabase() = default;

        /** @brief 절대 경로 → Resource/ 기준 전역 ID (`engine/...`, `game/<pack>/...`). 밖이면 empty. */
        static string toRelativePath( string_view absolutePath );

        /** @brief 리소스 상대 에셋의 사이드카 경로 (`foo.png` → `foo.png.meta`). */
        static string metaPathFor( string_view relativePath );

        // ------------------------------------------------------------------------------
        // 2) 등록 · 조회
        // ------------------------------------------------------------------------------
        /**
         * @brief 기존 .meta를 로드하거나 새로 만듭니다 (guid + sourcePath + 선택 imported).
         * @return GUID 문자열. 실패 시 null UUID 문자열.
         */
        Uuid ensureMeta( string_view relativePath, bool bImported = false );

        /** @brief .meta가 있으면 로드하고 등록합니다. 없거나 무효면 false. */
        bool registerExisting( string_view relativePath );

        /** @brief 상대 경로의 GUID를 찾습니다. 없으면 nullptr. */
        const Uuid* getGuid( string_view relativePath ) const;
        /** @brief GUID의 상대 경로를 찾습니다. 없으면 nullptr. */
        const string* getPath( const Uuid& guid ) const;

        /** @brief 절대 폴더를 스캔해 에셋을 등록하고 .meta를 로드/생성합니다 (*.meta 제외). */
        uint32 refreshFolder( string_view absoluteFolder, bool bCreateMissing = true );

        /** @brief 경로↔GUID 맵을 비웁니다. */
        void clear();

        // ------------------------------------------------------------------------------
        // 3) .meta I/O
        // ------------------------------------------------------------------------------
        /** @brief .meta 파일을 씁니다. */
        bool writeMetaFile( string_view relativePath, const Uuid& guid, bool bImported ) const;
        /** @brief .meta 파일을 로드합니다. */
        bool loadMetaFile( string_view relativePath, Uuid& outGuid, bool* pOutImported = nullptr ) const;

        mutable std::shared_mutex      _mutex;
        map<string, Uuid, std::less<>> _mapPathToGuid;
        unordered_map<Uuid, string>    _mapGuidToPath;
    };
} // namespace sw
