#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Uuid/Uuid.h"

#include <shared_mutex>

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
        // 1) 생성자 / 소멸자
        // ------------------------------------------------------------------------------
        AssetDatabase()  = default;
        ~AssetDatabase() = default;

        AssetDatabase( const AssetDatabase& )            = delete;
        AssetDatabase& operator=( const AssetDatabase& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) 경로 · 사이드카
        // ------------------------------------------------------------------------------
        /** @brief 절대 경로 → Resource/ 기준 전역 ID (`engine/...`, `game/<pack>/...`). 밖이면 empty. */
        static string toRelativePath( string_view absolutePath );

        /** @brief 리소스 상대 에셋의 사이드카 경로 (`foo.png` → `foo.png.meta`). */
        static string metaPathFor( string_view relativePath );

        // ------------------------------------------------------------------------------
        // 3) 등록 · 조회
        // ------------------------------------------------------------------------------
        /**
         * @brief 기존 .meta를 로드하거나 새로 만듭니다 (guid + sourcePath + 선택 imported).
         * @return GUID 문자열. 실패 시 null UUID 문자열.
         */
        Uuid ensureMeta( string_view relativePath, bool bImported = false );

        /** @brief 상대 경로와 GUID 매핑을 직접 등록/갱신합니다 (테스트/커스텀 로더용). */
        void registerMapping( string_view relativePath, const Uuid& guid );

        /** @brief .meta가 있으면 로드하고 등록합니다. 없거나 무효면 false. */
        bool registerExisting( string_view relativePath );

        /** @brief 상대 경로의 GUID를 스레드 안전하게 복사 조회합니다. */
        bool tryGetGuid( string_view relativePath, Uuid& outGuid ) const;

        /** @brief GUID의 상대 경로를 스레드 안전하게 복사 조회합니다. */
        bool tryGetPath( const Uuid& guid, string& outPath ) const;

        /** @brief 상대 경로의 GUID를 찾습니다. 없으면 nullptr. */
        const Uuid* getGuid( string_view relativePath ) const;

        /** @brief GUID의 상대 경로를 찾습니다. 없으면 nullptr. */
        const string* getPath( const Uuid& guid ) const;

        /** @brief 등록된 에셋 총 개수를 반환합니다. */
        size_t getAssetCount() const;

        /** @brief 절대 폴더를 스캔해 에셋을 등록하고 .meta를 로드/생성합니다 (*.meta 제외). */
        uint32 refreshFolder( string_view absoluteFolder, bool bCreateMissing = true );

        /**
         * @brief 리소스 루트 아래의 모든 `.meta` 를 재귀로 읽어 등록합니다(만들지는 않는다). 돌려주는 값은 등록 수.
         * @details 시작 시점에 표를 채우는 개발 빌드 경로다. 예전엔 `ensureMeta` 를 거친 에셋만 알아서, 이름을
         *          바꾼 프리팹의 GUID 복구가 "그 세션에서 먼저 로드됐을 때만" 동작했다 — 유니티의 GUID 표,
         *          언리얼의 AssetRegistry 는 시작부터 전체를 안다.
         */
        uint32 scanMetaFiles( string_view absoluteRoot );

        /**
         * @brief 쿠커가 만든 레지스트리(`<domain>/assetregistry.txt`)를 리소스 경로로 읽어 등록합니다. 없으면 0.
         * @details 배포 빌드는 `.meta` 를 싣지 않으므로(PackConfig `*.meta` 제외) 이 파일이 GUID 의 유일한 출처다.
         */
        uint32 loadRegistry( string_view registryRelativePath );

        /**
         * @brief 레지스트리 본문을 등록합니다. 한 줄에 `<guid> <sourcePath>`, `#` 으로 시작하면 주석. 돌려주는 값은 등록 수.
         * @details 형식은 `CookAssets.py` 의 `buildAssetRegistryInternal` 이 정본이다 — 둘이 어긋나면 배포본만 조용히 GUID 를 잃는다.
         */
        uint32 loadRegistryText( string_view text );

        /** @brief 경로↔GUID 맵을 비웁니다. */
        void clear();

        // ------------------------------------------------------------------------------
        // 4) .meta I/O
        // ------------------------------------------------------------------------------
        /** @brief .meta 파일을 씁니다. */
        bool writeMetaFile( string_view relativePath, const Uuid& guid, bool bImported ) const;

        /** @brief .meta 파일을 로드합니다. */
        bool loadMetaFile( string_view relativePath, Uuid& outGuid, bool* pOutImported = nullptr ) const;

    private:
        mutable std::shared_mutex      _mutex;
        map<string, Uuid, std::less<>> _mapPathToGuid;
        unordered_map<Uuid, string>    _mapGuidToPath;
    };
} // namespace sw
