/**
 * @file AssetScanCache.h
 * @brief 콘텐츠 트리 훑기의 디스크 캐시입니다 — 파일마다 (크기 · 쓰기 시각 도장, 그 파일에서 뽑은 바이트)를 들고 도장이 같으면 파일을 열지 않습니다.
 * @details 기동 훑기가 파일을 모두 열면 콘텐츠 수에 비례해 기동이 늘어난다. 언리얼 `CachedAssetRegistry.bin`(패키지마다 시각 도장), 유니티 `Library/SourceAssetDB`, Godot `filesystem_cache` 와 같은
 *          자리다. 뽑은 바이트의 뜻은 훑는 쪽의 것이다 — 정체성 색인(`AssetDatabase::scan`)은 머리 글, 에디터 색인(`EditorAssetIndex::scan`)은 GUID 와 참조 줄을 둔다.
 *          파일이 없거나 판이 다르면 빈 캐시다(모두 다시 읽는다) — 캐시라 폴백이 아니다. 막 쓴 파일은 두지 않는다(`ContentHash::isStampStable`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    /**
     * @class AssetScanCache
     * @brief 훑기 한 번의 캐시입니다: `load` → 파일마다 `find`(없으면 읽어 `record`) → `save`. 한 스레드가 부릅니다.
     * @details 이번 훑기가 찾거나 적지 않은 줄(사라진 파일)은 `save` 가 버린다.
     */
    class SW_API AssetScanCache
    {
    public:
        /** @param payloadVersion 뽑은 바이트의 판입니다. 뽑는 규칙을 바꾸면 올린다 — 다른 판의 파일은 빈 캐시로 읽는다. */
        explicit AssetScanCache( uint32 payloadVersion );

        /** @brief 이 기계의 캐시 파일(`<프로젝트>/Saved/<@p fileName>`)입니다. */
        static AbsolutePath makeLocalPath( string_view fileName );
        /** @brief @p cachePath 를 싣습니다. 없거나 판이 다르면 빈 채로 성공이고, 읽지 못했거나 깨졌으면 오류이며 비어 있습니다. */
        [[nodiscard]] expected<void> load( const AbsolutePath& cachePath );
        /**
         * @brief 이번 훑기가 쓴 줄을 @p cachePath 에 씁니다(원자적 쓰기). 실은 것과 달라진 줄이 없으면 쓰지 않습니다.
         * @return 쓰지 못했으면 오류입니다 — 함께 도는 다른 프로세스와 자리를 다툰 것일 수 있다(다음 훑기가 다시 쓴다).
         */
        [[nodiscard]] expected<void> save( const AbsolutePath& cachePath ) const;

        /** @brief @p key(리소스 id)의 줄이 있고 도장이 @p stamp 와 같으면 뽑은 바이트, 아니면 nullptr 입니다. 다음 `record` 까지 유효합니다. */
        [[nodiscard]] const vector<uint8>* find( const string& key, const FileStamp& stamp );
        /** @brief 읽은 파일의 줄을 적습니다. 막 쓴 파일(`ContentHash::isStampStable` 가 아닌 것)은 적지 않습니다. */
        void record( const string& key, const FileStamp& stamp, vector<uint8> payload );

    private:
        /** @brief 줄 하나입니다. */
        struct Entry
        {
            FileStamp     _stamp{};
            vector<uint8> _payload{};
            bool          _bUsed{ false }; ///< 이번 훑기가 찾거나 적었다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

    private:
        unordered_map<string, Entry> _mapEntry;
        uint32                       _payloadVersion;
        uint32                       _usedCount;    ///< `_bUsed` 인 줄 수
        uint8                        _bChanged : 1; ///< 이번 훑기가 줄을 새로 적었다
    };
} // namespace sw
