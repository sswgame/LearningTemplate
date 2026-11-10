/**
 * @file ModSupportSettings.h
 * @brief 게임이 모드에 허락하는 것(콘텐츠 판, 덮어쓸 수 있는 기본 리소스 접두)과 바깥 입력의 크기 한계입니다. 게임 팩 데이터(`game/<팩>/data/modsupport.xml`)에서 읽는다.
 * @details 정책 값은 코드 표가 아니라 데이터다(데이터 종류 `kDataKind`). 모든 칸이 필수이고 빠지면 읽기가 실패한다(기본값으로 물러나지 않는다).
 *
 *          <ModSupportSettings contentVersion="1" maxModCount="16" maxManifestBytes="16384" maxPackBytes="67108864" maxEntryCount="2048" maxEntryBytes="16777216">
 *              <Overridable prefix="game/empty/data/"/>
 *          </ModSupportSettings>
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLDocument;
    class XMLNode;

    /**
     * @struct ModSupportSettings
     * @brief 모드 정책과 한계입니다. 값은 `parseXMLText` 가 채운다.
     * @details 루트 속성(한계)은 리플렉션 칸이다 — `XMLFields::readFields` 가 필수(`Required`) · 범위(`Min = 1`, 0 인 한계는 모든 모드를 막는 오타다)를 본다.
     */
    REFLECT()
    struct SW_KIT_API ModSupportSettings
    {
        REFLECT_BODY();

        static constexpr const utf8* kDataKind    = "modsupport";
        static constexpr const utf8* kRootElement = "ModSupportSettings";

        PROPERTY( Required, Min = 1 )
        uint32 _contentVersion{ 0 }; ///< 게임 콘텐츠 판 — 매니페스트의 `minContentVersion` ~ `maxContentVersion` 안이어야 한다
        PROPERTY( Required, Min = 1 )
        uint32 _maxModCount{ 0 }; ///< 한 세션에 켜는 모드 수 상한
        PROPERTY( Required, Min = 1 )
        uint32 _maxManifestBytes{ 0 }; ///< 매니페스트 파일 크기 상한
        PROPERTY( Required, Min = 1 )
        uint64 _maxPackBytes{ 0 }; ///< 모드 팩 파일 크기 상한
        PROPERTY( Required, Min = 1 )
        uint32 _maxEntryCount{ 0 }; ///< 모드 팩 항목 수 상한
        PROPERTY( Required, Min = 1 )
        uint32         _maxEntryBytes{ 0 };    ///< 항목 하나의 풀린 크기 상한
        vector<string> _listOverridablePrefix; ///< 모드가 덮어쓸 수 있는 기본 리소스 id 접두(끝 `/`). 비면 덮어쓰기를 받지 않는다

        /** @brief 설정 글을 읽습니다. 모르는 원소 · 빠진 칸 · 0 인 한계 · 끝이 `/` 가 아니거나 정규형이 아닌 접두면 이유를 오류로 남기고 false 입니다. */
        [[nodiscard]] static bool parseXMLText( string_view text, string_view sourceName, ModSupportSettings& outSettings );
        /** @brief 읽은 문서(조건을 푼 글)를 설정으로 옮깁니다. 글 입구와 리소스 입구가 함께 쓴다. */
        [[nodiscard]] static bool parseDocument( const XMLDocument& doc, string_view sourceName, ModSupportSettings& outSettings );
        /** @brief 리소스 @p assetID 의 설정을 읽습니다(데이터 종류의 로더). */
        [[nodiscard]] static bool loadFromAsset( const AssetID& assetID, ModSupportSettings& outSettings );

        /** @brief 기본 리소스 @p assetID 를 모드가 덮어써도 되면 true 입니다(허락한 접두 아래). */
        bool isOverridable( string_view assetID ) const;

    private:
        /** @brief `<Overridable prefix>` 한 줄을 읽습니다(카탈로그 표의 손 읽기, `parseXMLText`). */
        [[nodiscard]] bool readOverridable( const XMLNode& node, string_view sourceName );
    };
} // namespace sw
