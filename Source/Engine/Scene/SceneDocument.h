/**
 * @file SceneDocument.h
 * @brief 씬 문서 모델과 XML/바이너리 직렬화입니다(씬 이름, 월드 설정, 엔티티 노드 목록, 프리팹 · 임베디드 상태, 쿠킹본의 스트리밍 셀).
 * @details 씬 형식 판(`SceneDocument::kAssetType._formatVersion`)이 맞지 않는 XML 은 읽지 않습니다 — 옛 판 리더를 두지 않고, 판을 올릴 때 저장소의 씬을
 *          한 번에 다시 씁니다.
 *
 *          조립 칸(씬 인스턴스 `scene` · 씬 상속 `_base` · 데이터 레이어 `layers` · `<SceneSettings>` 의 `_listDataLayer`)은 없어도 되는 칸이라 판을 올리지 않았다 — 이미 있는 씬은 그대로 읽힌다.
 *          씬 인스턴스는 읽을 때 평평한 엔티티 목록으로 펼치고(`expandSceneInstances`) 쿠킹본도 펼친 것을 담는다. 그래서 런타임의 짓는 길은 하나이고, 저장만 앵커 + 덮어쓴 것으로 되돌린다.
 *          셀과 같은 모델이다: 문서는 엔티티 목록이고, 문서를 나눈 조각을 `Scene::instantiateNodes` 한 길로 짓는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneSettings.h"

namespace sw
{
    /** @brief 씬 루트(`<Scene formatVersion name>`)의 칸입니다. 이름을 적지 않으면 파일 이름이다. `formatVersion` 은 에셋 형식이 읽는다. */
    REFLECT()
    struct SW_API SceneRootFields
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
    };

    /**
     * @brief 엔티티(`<entities><entity id name prefab>`)의 칸입니다. 상태(`<GameObject>`)와 프리팹 덮어쓰기는 자식 원소라 서브트리째 둔다.
     * @details 파일 id 는 0 이 아니어야 한다(`Scene::serializeToDocument` 가 늘 적는다) — 부착 · 핸들이 이 값으로 부모를 가리키고 쿠커가 엔티티를 찾는다.
     */
    REFLECT()
    struct SW_API SceneEntityFields
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 1 )
        uint64 _id{ 0 };
        PROPERTY()
        string _name{};
        PROPERTY()
        string _prefab{};
        /** @brief 씬 인스턴스의 앵커면 넣을 씬의 리소스 id 입니다(언리얼 Level Instance). 앵커 자리에 그 씬이 펼쳐진다. */
        PROPERTY()
        string _scene{};
        /** @brief 이 엔티티가 붙은 데이터 레이어 이름들입니다(쉼표로 가른다). 모두 켜졌을 때 오른다. */
        PROPERTY()
        string _layers{};
    };

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
             *          씬 인스턴스 앵커에서는 멤버에 덮어쓴 것(`<Part path>`)만 담습니다.
             */
            string _prefabOverrideXML;
            /** @brief 이 엔티티가 붙은 데이터 레이어 이름(정렬, 겹침 없음)입니다. 모두 켜졌을 때 오릅니다(`SceneDataLayerSubsystem`). 인스턴스 멤버는 앵커의 레이어를 더해 가집니다. */
            vector<string> _listDataLayer;
            /** @brief 씬 인스턴스의 앵커면 넣은 씬의 리소스 id 입니다. **저작 칸** — 쿠킹본은 펼친 결과만 싣고 이 칸을 비웁니다. */
            string _sceneInstance;
            /**
             * @brief 씬 인스턴스의 멤버면 그 자리의 앵커(가장 가까운)의 파일 id 입니다. 0 이면 멤버가 아닙니다.
             * @details 부모가 없는 멤버는 지을 때 이 앵커에 붙습니다(`Scene::instantiateNodes`). 쿠킹본에도 실립니다 — 프리팹 멤버의 상태는 쿠킹본에서도 프리팹 + 덮어쓴 것이라 부착이 들어 있지 않습니다.
             */
            uint64 _instanceAnchorFileID{ 0 };
            /**
             * @brief 글 상태(`_embeddedXML` · `_prefabOverrideXML` · `_listInstanceOverrideXML`)의 참조 id 를 평평한 파일 id 로 옮기는 XOR 값입니다. 0 이면 그대로입니다.
             * @details 멤버의 `_fileID` 도 내부 파일 id ^ 이 값입니다. 앵커의 파일 id 에서 정해지므로(`computeInstanceIDScope`) 같은 씬을 두 번 넣어도 id 가 겹치지 않고, 내부 씬을 고쳐도 흔들리지 않습니다.
             *          쿠킹된 바이너리 상태(`_embeddedStateBytes`)는 이미 평평한 id 라 이 값을 쓰지 않습니다.
             */
            uint64 _idScope{ 0 };
            /** @brief 멤버가 위에서부터 차례로 받은 덮어쓰기(`<PrefabOverrides>`)입니다. 안쪽 인스턴스의 것이 먼저입니다. 프리팹 상태 · 글 상태 위에 차례로 얹습니다. */
            vector<string> _listInstanceOverrideXML;
            /** @brief **저작 칸.** 문서 뿌리에서 멤버까지의 경로입니다(`5/7/3` — 앵커 파일 id 들 / 내부 파일 id). 앞 칸이 이 문서의 앵커이고 나머지가 그 앵커의 `<Part path>` 입니다. */
            string _instancePartPath;
            /** @brief **저작 칸.** 맨 위 앵커의 `<Part>` 가 이 멤버에 적은 것입니다. 안 올린 레이어의 멤버는 살아 있는 값이 없어 저장이 이것을 그대로 다시 적습니다. */
            string _instanceTopOverrideXML;
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
            vector<string>          _listDataLayer; ///< 이 셀 조각의 데이터 레이어(비면 레이어 없는 엔티티). 언리얼 World Partition 처럼 좌표 + 레이어 집합이 런타임 셀 하나다 — 켜고 끌 때 이 조각만 오르내린다
        };

        string                  _name;
        string                  _sourcePath;
        string                  _baseScenePath;       ///< 씬 상속의 부모(루트의 `_base`). 읽을 때 펼치고 저장이 차이로 쓴다(`saveXMLDelta`). 쿠킹본은 비어 있다
        SceneSettings           _sceneSettings{};     ///< 씬 파일의 `<SceneSettings>`(형식 판 2부터 필수)
        vector<SceneObjectNode> _listSceneObjectNode; ///< 늘 올라 있는 엔티티. 스트리밍하지 않는 씬은 모든 엔티티다
        vector<StreamingCell>   _listStreamingCell;   ///< 셀로 나눈 엔티티(쿠킹본에만, z · x 오름차순). XML 로는 쓰지 않는다
        bool                    _bValid{ false };     ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

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
        /** @brief XML 을 디스크 경로에 저장합니다. 셀로 나눈 문서(쿠킹본, `_listStreamingCell`)는 쓰지 않고 false 입니다 — 셀의 엔티티를 잃는다. */
        [[nodiscard]] SW_API bool saveXML( const AbsolutePath& absPath ) const;
        /**
         * @brief 씬 상속 문서를 부모 @p parent 와의 차이로 저장합니다(`_baseScenePath` 가 차 있어야 합니다).
         * @param parent 같은 직렬화기로 쓴 부모입니다(부모를 지어 `Scene::serializeToDocument` 로 쓴 것). 손으로 쓴 글과 비교하면 값이 같아도 모두 다르게 나온다(`DataInheritance::makeDelta`).
         */
        [[nodiscard]] SW_API bool saveXMLDelta( const AbsolutePath& absPath, const SceneDocument& parent ) const;
        /** @brief 쿠킹본에 실을 필요가 없는 저작 칸(앵커의 씬 경로 · `<Part>`, 멤버의 경로)을 비웁니다. 쿠커가 부릅니다. */
        SW_API void stripAuthoringInstanceData();
        /** @brief 앵커 파일 id 에서 멤버의 id 범위를 만듭니다(0 이 아니고, 같은 앵커면 늘 같다). */
        [[nodiscard]] SW_API static uint64 computeInstanceIDScope( uint64 anchorFileID );
        /**
         * @brief 멤버 경로(`_instancePartPath`)를 맨 위 앵커의 파일 id 와 그 안의 경로로 가릅니다.
         * @return 앵커와 경로로 갈리지 않으면 false 입니다.
         */
        [[nodiscard]] SW_API static bool splitInstancePartPath( string_view partPath, uint64& outAnchorFileID, string_view& outRelativePath );
        /** @brief 리소스 id 의 바이너리(SCN1)를 로드합니다(OS 경로는 받지 않습니다). 월드 설정과 셀까지 읽습니다. */
        [[nodiscard]] SW_API bool loadBinary( const AssetID& assetID );
        /** @brief 바이너리(SCN1)를 디스크 경로에 저장합니다. 월드 설정과 셀까지 씁니다. */
        [[nodiscard]] SW_API bool saveBinary( const AbsolutePath& absPath ) const;
        /** @brief 셀까지 모든 엔티티 수입니다. */
        SW_API size_t computeTotalEntityCount() const;

    private:
        /**
         * @brief XML 읽기 입구 둘(`loadXML` · `loadXMLFromDiskFile`)이 읽은 문서를 풉니다.
         * @details 리소스 id 와 디스크 경로는 입구가 형식으로 갈라 읽는다 — 안쪽이 열거형으로 고르면 같은 글이 어느 쪽으로도 흘러간다.
         */
        [[nodiscard]] bool readXMLSource( XMLDocument& doc, bool bRead, const string& absPath, AssetLoadScope& loadScope, vector<string>& inoutListChain );
        /** @brief 리소스 id 의 XML 을 읽습니다. @p inoutListChain 은 지금 펼치는 씬 사슬입니다(자기 포함 · 깊이 검사). */
        [[nodiscard]] bool loadXMLChained( const AssetID& assetID, vector<string>& inoutListChain );
        /** @brief 씬 인스턴스 앵커마다 넣은 씬의 엔티티를 앵커 뒤에 펼칩니다(런타임은 이 평평한 목록만 본다). 없는 씬 · 자기 포함 · 겹친 id 는 오류입니다. */
        [[nodiscard]] bool expandSceneInstances( string_view absPath, vector<string>& inoutListChain );
        /** @brief 문서를 XML 로 만듭니다. 읽는 쪽이 받지 않는 모양이면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool buildXML( XMLDocument& outDoc ) const;
    };

} // namespace sw
