/**
 * @file MaterialTagIndex.h
 * @brief 머티리얼 태그 색인(`materialindex.xml`)입니다. 머티리얼마다 셰이더 경로(Technique)와 define(키워드)을 적어, 읽지 않은 머티리얼도 키워드 · 셰이더 · 패스로 찾는다.
 * @details 팩 쿠킹이 도메인의 첫 팩에 쓰고(`PackCooker`), 배포 실행은 마운트한 도메인마다 읽어 합친다. 소스 실행(개발 · 쿠킹)은 같은 줄을 머티리얼을 읽어 만든다.
 *          언리얼 에셋 레지스트리 태그(`FAssetData::GetTagValue` — 쿠킹본에서도 머티리얼의 사용 플래그를 읽는다) 자리다. 유니티 런타임은 읽은 머티리얼만 훑으므로(FindObjectsOfTypeAll)
 *          에셋 수십만에서 쓸 수 없다. 판정은 읽은 머티리얼의 질의(`Material::isKeywordEnabled` · `isDrawnByPass`)와 같은 함수다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetGUID.h"
#include "Engine/Graphics/Material/MaterialTypes.h"

namespace sw
{
    class Material;

    /** @brief 색인 한 줄입니다. define 은 머티리얼의 에셋 상태 그대로다(`Material::getCachedShaderDefines` — 정렬). 실행 중 켠 런타임 스위치는 들지 않는다. */
    struct MaterialTagEntry
    {
        AssetGUID      _guid;
        string         _shaderPath;
        vector<string> _listDefine;
    };

    /**
     * @struct MaterialTagIndex
     * @brief 머티리얼 태그 색인입니다. 파일 머리말 참고.
     */
    struct SW_API MaterialTagIndex
    {
        /** @brief 도메인 팩 루트의 색인 파일 이름입니다. */
        static constexpr string_view kFileName = "materialindex.xml";
        /** @brief 색인 글의 판(루트 `formatVersion`)입니다. 꼴이 바뀌면 올린다. */
        static constexpr uint32 kFormatVersion = 0;

        vector<MaterialTagEntry> _listEntry;

        /** @brief 읽은 머티리얼 @p material 의 줄입니다(정체성 @p guid). */
        [[nodiscard]] static MaterialTagEntry makeEntry( const AssetGUID& guid, const Material& material );

        /** @brief 색인 글입니다(GUID 순 — 같은 입력은 같은 바이트). GUID 가 겹치면 그 까닭입니다. */
        [[nodiscard]] expected<string> makeText() const;
        /** @brief @p text 를 읽은 색인입니다. 꼴이 틀리거나(모르는 판 · 칸) GUID 가 겹치면 그 까닭입니다. */
        [[nodiscard]] static expected<MaterialTagIndex> parseText( string_view text, string_view sourceName );
        /**
         * @brief 도메인 @p listDomain(낮은 우선순위가 앞 — `PackManager::getMountedDomainsByPriority`)의 색인을 합칩니다. 뒤 도메인의 줄이 같은 GUID 의 앞 줄을 이긴다.
         * @details 배포 콘텐츠는 도메인마다 `<도메인>/materialindex.xml` 을 읽고, 없으면 오류다(다시 쿠킹한다). 소스 콘텐츠는 도메인의 `.material` 을 모두 읽어 줄을 만든다 —
         *          정체성이 없거나 읽지 못한 머티리얼은 건너뛰지 않고 오류다.
         */
        [[nodiscard]] static expected<MaterialTagIndex> loadMounted( const vector<string>& listDomain );

        /** @brief 키워드 @p keyword(define 이름, `=` 앞)를 켠 머티리얼입니다(`Material::isKeywordEnabled` 와 같은 판정). */
        [[nodiscard]] vector<AssetGUID> findByKeyword( string_view keyword ) const;
        /** @brief 셰이더 경로(Technique) @p shaderPath 로 그리는 머티리얼입니다(`Material::getShaderPath` 와 같은 글). */
        [[nodiscard]] vector<AssetGUID> findByShaderPath( string_view shaderPath ) const;
        /** @brief 패스 거르기 @p filter(`RenderGraphPassDesc::_resolvedMaterialFilter`)가 그리는 머티리얼입니다(`Material::isDrawnByPass` 와 같은 판정). */
        [[nodiscard]] vector<AssetGUID> findDrawnByPass( const MaterialPassFilter& filter ) const;
    };
} // namespace sw
