/**
 * @file PackChunkManifest.h
 * @brief 미뤄 마운트하는 청크의 목록(`packchunks.xml`)입니다 — 청크마다 팩 파일, 씬마다 그 씬을 열기 전에 마운트할 청크.
 * @details 언리얼 청크 pak(ChunkID 별 `pakchunkN`)과 유니티 Addressables(그룹 번들 + 카탈로그)의 자리다. 쿠커(`PackCooker`)가 팩 폴더에 쓰고,
 *          런타임은 기동 때 목록에 든 팩을 마운트하지 않고(`PackManager::scanAndMountPacks`) 씬을 열 때 그 씬의 청크만 마운트한다(`PackChunkLoader`).
 *          목록에 없는 팩(엔진 · 공용 · 첫 씬이 닿는 기본 청크, 이름 붙은 패턴 청크)은 기동 필수다. 데스크톱 · Android · 웹이 같은 파일을 쓴다(한 형식).
 *          id 는 정규화한 리소스 id(소문자 · `/`)다. 목록은 이름 · id 순으로 정렬해 쓴다(같은 쿠킹이면 같은 바이트).
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
        /** @brief 팩 폴더 안의 목록 파일 이름입니다. */
        static constexpr const utf8* kFileName = "packchunks.xml";

        vector<PackChunkFiles> _listChunk; ///< 이름순
        vector<PackSceneEntry> _listScene; ///< 씬 id 순

        /** @brief 목록 글을 읽습니다. 형식이 틀렸거나 팩 이름이 폴더를 벗어나거나 씬이 없는 청크를 가리키면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parse( string_view xmlText, string_view sourceName, PackChunkManifest& outManifest );
        /** @brief 목록 글을 만듭니다(목록은 이미 정렬돼 있어야 한다 — 쿠커가 정렬해 넘긴다). */
        static string makeText( const PackChunkManifest& manifest );

        /** @brief @p packFileName(파일 이름, 대소문자 무시)이 어느 청크의 팩이면 true 입니다 — 기동 때 마운트하지 않는다. */
        [[nodiscard]] bool isDeferredPack( string_view packFileName ) const;
        /** @brief 씬 @p sceneID(리소스 id — 정규화한다)를 열기 전에 마운트할 팩 파일 이름을 @p outListPackFileName 뒤에 덧붙입니다. 목록에 없는 씬은 덧붙일 것이 없다. */
        void appendScenePackFiles( string_view sceneID, vector<string>& outListPackFileName ) const;
    };
} // namespace sw
