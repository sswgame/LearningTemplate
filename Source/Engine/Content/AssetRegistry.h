/**
 * @file AssetRegistry.h
 * @brief 런타임의 GUID ↔ 리소스 id 색인(`AssetRegistry`)과 참조 글 표기 `guid:<32자>` 입니다.
 * @details 정체성은 무작위 GUID(`AssetGUID`)이고 경로는 그 에셋이 지금 있는 자리일 뿐이다 — 참조는 GUID 를 적고(`AssetRef<T>` 의 저장 꼴), 읽을 때 이 색인으로 경로를 푼다.
 *          색인은 층을 마운트 순서(낮은 우선순위 먼저)로 합친다. 배포 구성은 마운트한 도메인 팩마다 `assetindex.xml`(`AssetPackIndex`)이 한 층이고, 개발 실행(느슨한 소스 트리)은
 *          저작 색인(`AssetDatabase::scan`) 하나가 층이다(`AssetManager::mountContent` 가 건다). 뒤 층(패치 · DLC)의 줄이 앞 층의 같은 GUID 를 이기고, 한 층 안의 겹침은 오류다.
 *          언리얼 Asset Registry(팩 조각을 마운트마다 합친다), 유니티 Addressables 카탈로그, Godot 4 `ResourceUID`(uid:// → 경로 표)와 같은 자리다.
 *          층이 Content(1)라 직렬화(`AssetRefBase`)가 읽는다 — 층을 읽고 합치는 일(팩 · 디스크)은 에셋 층(2)이 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetGUID.h"

namespace sw
{
    /** @brief 색인 한 줄입니다 — 리소스 id 는 도메인부터 적은 전체 id(`engine/x.xml`)다. */
    struct AssetRegistryEntry
    {
        AssetGUID _guid;
        string    _assetID;
    };

    /** @brief 합칠 층 하나입니다 — 팩 하나의 색인 또는 저작 색인. `_sourceName` 은 오류에 적는 이름이다. */
    struct AssetRegistryLayer
    {
        string                     _sourceName;
        vector<AssetRegistryEntry> _listEntry;
    };

    /**
     * @class AssetRegistry
     * @brief 합친 GUID ↔ 리소스 id 색인입니다(값 형식). 프로세스 표는 `install` 로 걸고 `resolve` 로 묻는다 — 읽기는 로드 워커 · IO 스레드에서도 온다.
     */
    class SW_API AssetRegistry
    {
    public:
        /** @brief 참조 글 표기의 머리입니다 — `guid:` 뒤에 `AssetGUID` 글 표기(소문자 16진 32 자). Godot 4 `uid://` 자리다. */
        static constexpr string_view kReferencePrefix = "guid:";

        /**
         * @brief 층들을 마운트 순서(@p listLayer 의 앞이 낮은 우선순위)로 합칩니다.
         * @details 뒤 층의 줄이 같은 GUID 의 앞 줄을 이긴다(옮긴 에셋의 옛 경로는 비운다). 뒤 층이 앞 층의 다른 GUID 가 쓰던 경로를 가지면 그 앞 GUID 는 풀리지 않는다 —
         *          그 자리의 파일은 이제 다른 에셋이다. 한 층 안에서 GUID 나 경로(대소문자 무시)가 겹치면 고르지 않고 오류다.
         */
        [[nodiscard]] static expected<AssetRegistry> merge( const vector<AssetRegistryLayer>& listLayer );

        /** @brief @p guid 의 리소스 id 입니다(복사). 없으면 없음입니다. */
        [[nodiscard]] optional<string> findPath( const AssetGUID& guid ) const;
        /** @brief 리소스 id @p assetID 의 GUID 입니다. 대소문자는 가리지 않습니다. 없으면 없음입니다. */
        [[nodiscard]] optional<AssetGUID> findGUID( string_view assetID ) const;
        /** @brief 색인의 에셋 수입니다. */
        [[nodiscard]] size_t getCount() const { return _mapGUIDToPath.size(); }

        // ------------------------------------------------------------------------------
        // 프로세스 표 — 에셋 매니저가 콘텐츠를 마운트할 때 건다(엔진 서비스 서브시스템이 든다). 엔진이 서기 전에는 비었다.
        // ------------------------------------------------------------------------------
        /** @brief @p registry 를 프로세스 표로 겁니다(있던 표를 바꾼다). */
        static void install( AssetRegistry registry );
        /** @brief 프로세스 표를 비웁니다(엔진 종료 · 테스트). */
        static void clear();
        /** @brief 프로세스 표에서 @p guid 의 리소스 id 입니다. 표가 없거나 줄이 없으면 없음입니다. */
        [[nodiscard]] static optional<string> resolve( const AssetGUID& guid );
        /** @brief 프로세스 표에서 리소스 id @p assetID 의 GUID 입니다. 표가 없거나 줄이 없으면 없음입니다. */
        [[nodiscard]] static optional<AssetGUID> resolveGUID( string_view assetID );

        // ------------------------------------------------------------------------------
        // 참조 글 표기
        // ------------------------------------------------------------------------------
        /** @brief @p guid 의 참조 글(`guid:<32자>`)입니다. */
        [[nodiscard]] static string makeReferenceText( const AssetGUID& guid );
        /** @brief @p text 가 GUID 참조 꼴(`guid:` 로 시작)이면 true 입니다 — 뒤가 틀려도 true 다(부르는 쪽이 `parseReferenceText` 로 읽고 못 읽으면 오류로 멈춘다). */
        [[nodiscard]] static bool isReferenceText( string_view text );
        /** @brief GUID 참조 글 @p text 의 GUID 입니다. 머리가 없거나 뒤가 글 표기가 아니면 없음입니다(고쳐 읽지 않는다). */
        [[nodiscard]] static optional<AssetGUID> parseReferenceText( string_view text );

    private:
        unordered_map<AssetGUID, string> _mapGUIDToPath;
        unordered_map<string, AssetGUID> _mapPathToGUID; ///< 키는 소문자 리소스 id
    };
} // namespace sw
