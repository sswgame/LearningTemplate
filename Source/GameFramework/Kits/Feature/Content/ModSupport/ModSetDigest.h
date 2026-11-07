/**
 * @file ModSetDigest.h
 * @brief 한 세션에 켠 모드 구성(마운트 순서의 id · 판 · 팩 내용 해시)과 그 구성 해시입니다. 서버 · 클라이언트 대조와 세이브 호환 확인이 이 값 하나를 본다.
 * @details 같은 모드 집합이면 요청 순서와 상관없이 같은 마운트 순서 · 같은 해시다(`ModResolver`). 모드가 없으면 빈 구성이고 해시는 0 이다 —
 *          모드 없는 실행의 와이어 버전 · 세이브는 그대로다(`foldWireVersion`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModManifest.h"

namespace sw
{
    /** @struct ModDigestEntry @brief 켠 모드 하나입니다. */
    struct ModDigestEntry
    {
        string     _id;
        ModVersion _version;
        uint64     _contentHash{ 0 }; ///< 마운트한 팩 파일 바이트의 해시(FNV-1a 64) — 같은 판의 다른 내용을 가른다
    };

    /**
     * @struct ModSetDigest
     * @brief 켠 모드 구성입니다. 항목은 마운트 순서(낮은 우선순위 → 높은)다.
     */
    struct SW_KIT_API ModSetDigest
    {
        vector<ModDigestEntry> _listEntry;
        uint64                 _configHash{ 0 }; ///< 항목 전체의 해시(`make`). 빈 구성은 0

        /** @brief 마운트 순서의 항목으로 구성과 해시를 만듭니다. */
        static ModSetDigest make( vector<ModDigestEntry> listEntry );

        /**
         * @brief 넷 와이어 버전에 이 구성을 섞습니다(`NetEndpointConfig::_wireVersion`). 빈 구성이면 @p wireVersion 그대로입니다.
         * @details 모드 구성이 다른 클라이언트는 핸드셰이크에서 `VersionMismatch` 로 갈린다 — 연결된 뒤에 어긋남을 찾지 않고 형식으로 막는다.
         *          언리얼이 콘텐츠 해시를 넷 버전에 섞는 자리(`FNetworkVersion::GetLocalNetworkVersionOverride`)와 같다(기억에 의존).
         */
        uint32 foldWireVersion( uint32 wireVersion ) const;

        /**
         * @brief @p other(상대 — 서버가 보낸 구성 · 세이브에 적힌 구성)와 다른 첫 곳을 사람이 읽는 글로 돌려줍니다. 같으면 빈 글입니다.
         * @details 핸드셰이크 거절은 이유를 싣지 않으므로, 서버 목록 · 로비가 받은 구성을 이 글로 보여 준다.
         */
        string describeDifference( const ModSetDigest& other ) const;

        /**
         * @brief 세이브에 적힌 구성 @p saved 를 지금 구성으로 읽어도 되면 true 입니다. 세이브의 모드가 지금 꺼졌거나 판이 다르면 이유를 오류로 남기고 false 입니다.
         * @details 새로 켠 모드는 받는다(세이브에 없던 내용이 더해질 뿐이다). 제거된 모드의 데이터를 가리키는 세이브는 읽지 않는다 — 없는 리소스를 조용히 비우지 않는다.
         */
        [[nodiscard]] bool checkSaveCompatible( const ModSetDigest& saved ) const;

        /** @brief 세이브 · 로그에 적는 글입니다(`id@1.2.3#해시16자리;…`, 빈 구성은 빈 글). `parseText` 의 짝입니다. */
        string toText() const;
        /** @brief `toText` 의 글을 읽습니다. 틀리면 이유를 오류로 남기고 false 입니다. */
        [[nodiscard]] static bool parseText( string_view text, ModSetDigest& outDigest );

        /** @brief @p id 의 항목입니다. 없으면 nullptr 입니다. */
        const ModDigestEntry* findEntry( string_view id ) const;
    };
} // namespace sw
