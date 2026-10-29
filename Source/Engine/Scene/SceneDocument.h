/**
 * @file SceneDocument.h
 * @brief 씬 문서 모델과 XML/바이너리 직렬화입니다(씬 이름, 월드 설정, 엔티티 노드 목록, 프리팹 · 임베디드 상태, 쿠킹본의 스트리밍 셀).
 * @details 씬 형식 판(`SceneDocument::kAssetType._formatVersion`)이 맞지 않는 XML 은 읽지 않습니다 — 옛 판 리더를 두지 않고, 판을 올릴 때 저장소의 씬을
 *          한 번에 다시 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Scene/SceneSettings.h"

namespace sw
{
    class AssetLoadScope;
    class XMLDocument;

    // ------------------------------------------------------------------------------
    // SceneDocument: 씬 메타데이터와 엔티티 노드 목록
    // ------------------------------------------------------------------------------
    struct SceneDocument
    {
        /** @brief 씬 파일의 접미사입니다 — 저작 소스, 쿠킹본, 확장자 없는 이름(쿠킹본으로 읽는다). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            {".scene.xml",  AssetSuffixRole::CookSource},
            {".scene.bin",      AssetSuffixRole::Cooked},
            {    ".scene", AssetSuffixRole::CookedAlias},
        };
        /**
         * @brief 씬 에셋 종류입니다(등록은 `SceneDocument.cpp` 의 `SW_ASSET_TYPE`).
         * @details 판 1: 프리팹 엔티티가 프리팹 경로 + 덮어쓴 것(`<PrefabOverrides>`)을 싣는다. 판 2: 씬 루트에 월드 설정(`<SceneSettings>`, 스트리밍 여부 · 격자)이
         *          반드시 있다. 판이 2 가 아닌 씬 파일은 읽지 않는다 — 이관기를 두지 않고 저장소의 씬을 다시 썼다.
         */
        static constexpr AssetTypeDef kAssetType{ "Scene", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), ".scene.bin", 2, {} };

        /** @brief 씬 문서 안의 엔티티 하나입니다(프리팹 참조 또는 임베디드 GameObject 상태). */
        struct SceneObjectNode
        {
            /**
             * @brief 이 파일 안에서 엔티티를 가리키는 id 입니다(유니티의 fileID 자리). 파일에 적힌 엔티티는 늘 0 이 아닙니다.
             * @details 다른 엔티티의 부착(`SceneComponent::_attachOwnerID`)이 이 값으로 부모를 가리킵니다. 런타임 오브젝트 id 와 다른 공간입니다 —
             *          씬이 런타임 id ↔ 파일 id 표를 들고 저장할 때마다 같은 값을 다시 씁니다(저장할 때마다 파일이 흔들리지 않게).
             */
            uint64 _fileID{ 0 };
            string _name;
            string _prefab;
            string _prefabGUID;
            string _embeddedXML;
            /**
             * @brief 쿠킹된 리플렉션 바이너리 상태입니다. **비어 있지 않으면 XML 대신 이것을 씁니다.**
             *
             * XML 은 사람이 고치는 원본이고 이것은 그것을 쿠킹한 것입니다. 둘 다 실리는 일은 없습니다.
             * 쿠커가 왕복 검증에 성공한 엔티티만 이쪽에 담고, 실패하면 XML 을 그대로 남깁니다.
             */
            vector<uint8> _embeddedStateBytes;
            /**
             * @brief 프리팹 인스턴스가 프리팹과 다른 점입니다(`<PrefabOverrides>` — `PrefabOverrides`). 비어 있으면 프리팹 그대로입니다.
             * @details 프리팹 엔티티는 이것만 싣고 상태(`_embeddedXML` · `_embeddedStateBytes`)는 비웁니다 — 로드가 프리팹을 지은 뒤 이것을 얹으므로
             *          프리팹을 고치면 놓인 인스턴스에 퍼집니다. 상태가 실린 프리팹 엔티티(프리팹을 읽지 못한 채 저장한 것)는 그 상태가 그대로 기준입니다.
             */
            string _prefabOverrideXML;
        };

        /**
         * @brief 스트리밍하는 씬의 셀 하나와 그 셀에 놓인 엔티티입니다. **쿠킹본에만 있습니다**(`SceneCooker` 가 나눈다).
         * @details 셀의 엔티티는 같은 셀의 엔티티나 기본 목록(`_listSceneObjectNode`, 늘 올라 있다)의 엔티티만 파일 id 로 가리킨다 — 쿠커가 그 밖의 참조를
         *          쿠킹 오류로 멈춘다(`StreamingCellGrid::findCrossCellReferences`).
         */
        struct StreamingCell
        {
            StreamingCellCoord      _cell{};
            vector<SceneObjectNode> _listSceneObjectNode;
        };

        string                  _name;
        string                  _sourcePath;
        SceneSettings           _sceneSettings{};     ///< 씬 파일의 `<SceneSettings>`(형식 판 2부터 필수)
        vector<SceneObjectNode> _listSceneObjectNode; ///< 늘 올라 있는 엔티티. 스트리밍하지 않는 씬은 모든 엔티티다
        vector<StreamingCell>   _listStreamingCell;   ///< 셀로 나눈 엔티티(쿠킹본에만, z · x 오름차순). XML 로는 쓰지 않는다
        bool                    _bValid{ false };

        /** @brief 빌드(Shipping/Dev)와 파일 존재 여부에 따라 알맞은 포맷(바이너리 우선)으로 로드합니다. */
        [[nodiscard]] SW_API bool load( const AssetID& assetID );
        /** @brief 리소스 id 의 XML 을 로드합니다. OS 경로는 받지 않습니다(디스크 파일은 `loadXMLFromDiskFile`). */
        [[nodiscard]] SW_API bool loadXML( const AssetID& assetID );
        /**
         * @brief **디스크 파일**에서 XML 을 로드합니다. 리소스 id 로 풀지 않고 팩을 보지 않습니다.
         * @details 소스 트리를 디스크로 훑은 쿠커 · 테스트용입니다 — OS 경로를 `loadXML` 에 넣으면 모든 구성에서 오류로 거절합니다.
         *          게임 런타임 코드는 쓰지 않습니다.
         */
        [[nodiscard]] SW_API bool loadXMLFromDiskFile( string_view diskPath );
        /** @brief XML 을 리소스 상대/절대 경로에 저장합니다. 셀로 나눈 문서(쿠킹본, `_listStreamingCell`)는 쓰지 않고 false 입니다 — 셀의 엔티티를 잃는다. */
        [[nodiscard]] SW_API bool saveXML( string_view path ) const;
        /** @brief 리소스 id 의 바이너리(SCN1)를 로드합니다(OS 경로는 받지 않습니다). 월드 설정과 셀까지 읽습니다. */
        [[nodiscard]] SW_API bool loadBinary( const AssetID& assetID );
        /** @brief 바이너리(SCN1)를 리소스 상대/절대 경로에 저장합니다. 월드 설정과 셀까지 씁니다. */
        [[nodiscard]] SW_API bool saveBinary( string_view path ) const;
        /** @brief 셀까지 모든 엔티티 수입니다. */
        SW_API size_t computeTotalEntityCount() const;

    private:
        /**
         * @brief XML 읽기 입구 둘(`loadXML` · `loadXMLFromDiskFile`)이 읽은 문서를 풉니다.
         * @details 리소스 id 와 디스크 경로는 입구가 형식으로 갈라 읽는다 — 안쪽이 열거형으로 고르면 같은 글이 어느 쪽으로도 흘러간다.
         */
        [[nodiscard]] bool readXMLSource( XMLDocument& doc, bool bRead, const string& absPath, AssetLoadScope& loadScope );
    };

} // namespace sw
