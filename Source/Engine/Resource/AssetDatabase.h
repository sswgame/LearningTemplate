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
     * @brief 메모리에 올린 에셋 식별 표(GUID ↔ 경로)입니다. 사이드카는 `relativePath + ".meta"` 입니다.
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
        /** @brief 절대 경로를 Resource/ 기준 전역 ID(`engine/...`, `game/<pack>/...`)로 바꿉니다. Resource 밖이면 빈 문자열입니다. */
        static string toRelativePath( string_view absolutePath );

        /** @brief 리소스 상대 에셋의 사이드카 경로입니다(`foo.png` → `foo.png.meta`). */
        static string metaPathFor( string_view relativePath );

        // ------------------------------------------------------------------------------
        // 3) 등록 · 조회
        // ------------------------------------------------------------------------------
        /**
         * @brief 기존 .meta 를 로드하거나 새로 만듭니다(guid + sourcePath + 선택 imported).
         * @return GUID 입니다. 실패하면 null UUID 입니다.
         */
        Uuid ensureMeta( string_view relativePath, bool bImported = false );

        /** @brief 상대 경로와 GUID 매핑을 직접 등록하거나 갱신합니다(테스트 · 커스텀 로더용). */
        void registerMapping( string_view relativePath, const Uuid& guid );

        /** @brief .meta 가 있으면 로드하고 등록합니다. 없거나 무효면 false 입니다. */
        bool registerExisting( string_view relativePath );

        /**
         * @brief 상대 경로의 GUID 를 찾아 복사합니다(스레드 안전). 경로는 안에서 정규화합니다.
         * @warning **이 표에서 값을 빌려 나가는 방법은 없습니다. 복사만 있습니다.** 예전에는 원소를
         *          가리키는 `getGuid`/`getPath` 가 함께 있었는데, 둘 다 잠금을 놓은 뒤에
         *          포인터를 반환했습니다. `_mapPathToGuid` 는 **정렬된 벡터**이고
         *          `_mapGuidToPath` 는 **밀집 배열**이라, 다른 스레드의 등록 하나가 원소를
         *          통째로 옮깁니다. 앞 키 자리에 하나만 끼어들어도 그 뒤가 모두 밀립니다.
         *          호출부 다섯 곳은 모두 받자마자 값을 복사하고 있었으므로, 빌려 주는 쪽을
         *          없앴습니다.
         */
        bool tryGetGuid( string_view relativePath, Uuid& outGuid ) const;

        /** @brief GUID 의 상대 경로를 찾아 복사합니다(스레드 안전). */
        bool tryGetPath( const Uuid& guid, string& outPath ) const;

        /** @brief 등록된 에셋 총 개수를 반환합니다. */
        size_t getAssetCount() const;

        /** @brief 절대 경로 폴더를 훑어 에셋을 등록하고 .meta 를 로드하거나 만듭니다(*.meta 제외). */
        uint32 refreshFolder( string_view absoluteFolder, bool bCreateMissing = true );

        /**
         * @brief 리소스 루트 아래의 모든 `.meta` 를 재귀로 읽어 등록합니다(만들지는 않습니다). 반환값은 등록 수입니다.
         * @details 시작 시점에 표를 채우는 개발 빌드 경로입니다. 예전에는 `ensureMeta` 를 거친 에셋만 알아서, 이름을
         *          바꾼 프리팹의 GUID 복구가 "그 세션에서 먼저 로드됐을 때만" 동작했습니다. 유니티의 GUID 표,
         *          언리얼의 AssetRegistry 는 시작부터 전체를 압니다.
         */
        uint32 scanMetaFiles( string_view absoluteRoot );

        /**
         * @brief 쿠커가 만든 레지스트리(`<domain>/assetregistry.txt`)를 리소스 경로로 읽어 등록합니다. 없으면 0 입니다.
         * @details 배포 빌드는 `.meta` 를 싣지 않으므로(PackConfig `*.meta` 제외) 이 파일이 GUID 의 유일한 출처입니다.
         */
        uint32 loadRegistry( string_view registryRelativePath );

        /**
         * @brief 레지스트리 본문을 등록합니다. 한 줄에 `<guid> <sourcePath>` 이고 `#` 으로 시작하면 주석입니다. 반환값은 등록 수입니다.
         * @details 형식은 `CookAssets.py` 의 `buildAssetRegistryInternal` 이 기준입니다. 둘이 어긋나면 배포본만 조용히 GUID 를 잃습니다.
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
