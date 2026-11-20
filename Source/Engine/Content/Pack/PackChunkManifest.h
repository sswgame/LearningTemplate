/**
 * @file PackChunkManifest.h
 * @brief 미뤄 마운트하는 청크의 목록(도메인마다 `packchunks.<도메인>.xml`)입니다 — 청크마다 팩 파일, 씬마다 그 씬을 열기 전에 마운트할 청크.
 * @details 언리얼 청크 pak(ChunkID 별 `pakchunkN`)과 유니티 Addressables(그룹 번들 + 카탈로그)의 자리다. 쿠커(`PackCooker`)가 팩 폴더에 쓰고,
 *          런타임은 기동 때 목록에 든 팩을 마운트하지 않고(`PackManager::scanAndMountPacks`) 씬을 열 때 그 씬의 청크만 마운트한다(`PackChunkLoader`).
 *          목록에 없는 팩(엔진 · 공용 · 첫 씬이 닿는 기본 청크, 이름 붙은 패턴 청크)은 기동 필수다. 데스크톱 · Android · 웹이 같은 파일을 쓴다(한 형식).
 *          id 는 정규화한 리소스 id(소문자 · `/`)다. 목록은 이름 · id 순으로 정렬해 쓴다(같은 쿠킹이면 같은 바이트).
 *          파일은 팩 도메인마다 하나이고 그 도메인의 청크(이름이 `<도메인 줄기>.<청크>` — `game_playground.scene_playground_alpha`)와 그 도메인의 씬만 담는다 — 한 게임만 구운 쿠킹이
 *          다른 게임의 목록을 덮지 않는다(팩 파일과 같은 단위). 씬은 다른 도메인의 청크(`common.shared`)를 가리킬 수 있어 런타임은 폴더의 목록을 모두 합친다(`merge`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 청크 하나와 그 청크를 이루는 팩 파일 이름(팩 폴더 안, 도메인마다 하나 이상)입니다. */
    struct PackChunkFiles
    {
        string         _name;
        vector<string> _listPackFileName;
    };

    /** @brief 씬 하나와 그 씬을 열기 전에 마운트할 청크 이름(기동 필수 청크는 적지 않는다)입니다. */
    struct PackSceneEntry
    {
        string         _sceneID;
        vector<string> _listChunk;
    };

    /**
     * @struct PackChunkManifest
     * @brief 파일 머리말 참고.
     */
    struct SW_API PackChunkManifest
    {
        /** @brief 목록 파일 이름의 앞뒤입니다(`packchunks.<도메인 줄기>.xml`). */
        static constexpr string_view kFilePrefix = "packchunks.";
        static constexpr string_view kFileSuffix = ".xml";

        vector<PackChunkFiles> _listChunk; ///< 이름순
        vector<PackSceneEntry> _listScene; ///< 씬 id 순

        /** @brief 목록 글을 읽습니다. 형식이 틀렸거나 팩 이름이 폴더를 벗어나거나 씬이 없는 청크를 가리키면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parse( string_view xmlText, string_view sourceName, PackChunkManifest& outManifest );
        /** @brief 목록 글을 만듭니다(목록은 이미 정렬돼 있어야 한다 — 쿠커가 정렬해 넘긴다). */
        static string makeText( const PackChunkManifest& manifest );
        /** @brief 도메인(`game/playground`)의 줄기(`game_playground` — 팩 파일 이름과 같은 납작한 이름)입니다. */
        static string makeDomainStem( string_view domain );
        /** @brief 도메인의 목록 파일 이름(`packchunks.game_playground.xml`)입니다. */
        static string makeFileName( string_view domain );
        /** @brief @p fileName 이 목록 파일 이름(`packchunks.<줄기>.xml`, 대소문자 무시)이면 true 입니다. */
        [[nodiscard]] static bool isFileName( string_view fileName );
        /**
         * @brief 다른 도메인의 목록 @p other 를 합칩니다(정렬은 유지한다).
         * @return 같은 청크 이름 · 같은 씬이 두 목록에 있으면(같은 도메인을 두 파일이 적었다) 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool merge( const PackChunkManifest& other, string_view sourceName );

        /** @brief @p packFileName(파일 이름, 대소문자 무시)이 어느 청크의 팩이면 true 입니다 — 기동 때 마운트하지 않는다. */
        [[nodiscard]] bool isDeferredPack( string_view packFileName ) const;
        /**
         * @brief 씬 @p sceneID(리소스 id — 정규화한다)를 열기 전에 마운트할 팩 파일 이름을 @p outListPackFileName 뒤에 덧붙입니다. 목록에 없는 씬은 덧붙일 것이 없다.
         * @return 씬이 합친 목록에 없는 청크를 가리키면(그 청크의 도메인을 다른 쿠킹이 다시 구웠다) 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool appendScenePackFiles( string_view sceneID, vector<string>& outListPackFileName ) const;
    };
} // namespace sw
