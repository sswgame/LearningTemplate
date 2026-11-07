/**
 * @file ModAllowList.h
 * @brief 서버 운영자가 허락한 모드 목록(id · 판 · 팩 내용 해시)입니다. 서버는 이 목록에 정확히 맞는 모드만 켠다.
 * @details 팩의 CRC 는 손상 검사이고 패치 서명(`PackPatchTrust`)은 패치 사슬의 것이다 — 모드의 출처 신뢰는 운영자가 고른 내용 해시로 정한다(같은 id · 판의 다른 내용을 거절).
 *          클라이언트 대조는 같은 구성 해시로 한다(`ModSetDigest::foldWireVersion` — 다르면 핸드셰이크가 갈린다).
 *
 *          <ModAllowList>
 *              <Mod id="demo_lantern" version="1.0.0" contentHash="0123456789abcdef"/>
 *          </ModAllowList>
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSetDigest.h"

namespace sw
{
    class XMLNode;

    /**
     * @struct ModAllowList
     * @brief 허락한 모드 목록입니다. 빈 목록은 모드를 하나도 허락하지 않는다.
     */
    struct SW_KIT_API ModAllowList
    {
        static constexpr const utf8* kRootElement = "ModAllowList";

        vector<ModDigestEntry> _listEntry;

        /** @brief 목록 글을 읽습니다. 모르는 원소 · 빠진 칸 · 틀린 id · 판 · 해시(16 진수 16 자리) · 같은 id 두 번이면 이유를 오류로 남기고 false 입니다. */
        [[nodiscard]] static bool parseXMLText( string_view text, string_view sourceName, ModAllowList& outList );

        /** @brief 구성 @p digest 의 모든 모드가 id · 판 · 내용 해시까지 목록에 있으면 true 입니다. 아니면 첫 모드의 이유를 오류로 남기고 false 입니다. */
        [[nodiscard]] bool checkDigest( const ModSetDigest& digest ) const;

    private:
        /** @brief `<Mod>` 한 줄을 읽습니다(카탈로그 표의 손 읽기, `parseXMLText`). */
        [[nodiscard]] bool readMod( const XMLNode& node, string_view sourceName );
    };
} // namespace sw
