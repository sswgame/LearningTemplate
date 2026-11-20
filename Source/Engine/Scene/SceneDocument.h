/**
 * @file SceneDocument.h
 * @brief 씬 문서 모델과 XML/바이너리 직렬화입니다(씬 이름, 월드 설정, 엔티티 노드 목록, 프리팹 · 임베디드 상태, 쿠킹본의 스트리밍 셀).
 * @details 씬 형식 판(`SceneDocument::kAssetType._formatVersion`)보다 옛 XML 은 등록된 판 올림 단계(`SW_ASSET_FORMAT_UPGRADE`)로 올려 읽고, 사슬이 끊기거나 새 판이면
 *          읽지 않습니다. 판을 올릴 때 저장소의 씬도 한 번에 다시 쓴다(판 3 — 형제 순서 칸 이름, 설계 E-6).
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
#include "Core/Math/VectorMath.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/PrefabAssetKind.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneSettings.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

namespace sw
{
    /** @brief 씬 루트(`<Scene formatVersion name>`)의 칸입니다. 이름을 적지 않으면 파일 이름이다. `formatVersion` 은 에셋 형식이 읽는다. */
    REFLECT()
    struct SW_API SceneRootParams
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
    struct SW_API SceneEntityParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 1 )
        uint64 _id{ 0 };
        PROPERTY()
        string _name{};
        /** @brief 프리팹 엔티티의 프리팹입니다 — 저장 꼴은 GUID 참조(`AssetRefBase`). 경로 글 칸이었을 때는 다른 칸의 `AssetRef<PrefabAsset>` `_prefab` 과 이름이 같아 GUID 이전 도구가 칸 꼴을 가르지 못했다. */
        PROPERTY()
        AssetRef<PrefabAsset> _prefab{};
        /** @brief 씬 인스턴스의 앵커면 넣을 씬의 리소스 id 입니다(언리얼 Level Instance). 앵커 자리에 그 씬이 펼쳐진다. */
        PROPERTY()
        string _scene{};
        /** @brief 이 엔티티가 붙은 데이터 레이어 이름들입니다(쉼표로 가른다). 모두 켜졌을 때 오른다. */
        PROPERTY()
        string _layers{};
    };

    class AssetLoadScope;
    class XMLDocument;

    /** @brief 씬 인스턴스의 멤버를 쿠킹이 묶는지입니다(`SceneObjectNode::_instancePacking`). */
    enum class SceneInstancePacking : uint8
    {
        Separate,     ///< 멤버를 엔티티 그대로 싣는다
        StaticMeshes, ///< 정적 메시 멤버를 인스턴스 메시로 묶는다(언리얼 Packed Level Actor)
    };

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
        static constexpr AssetTypeDef kAssetType{ "Scene", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), ".scene.bin", 3, {} };

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
            /**
             * @brief **런타임 칸.** 쿠킹된 바이너리 상태(`_embeddedStateBytes`)의 참조 id 에 XOR 하는 값입니다. 씬 파일 · 쿠킹본에서 읽은 엔티티는 0 이다(바이너리 상태는 이미 평평한 id).
             * @details 런타임 하위 레벨이 쿠킹본을 자기 앵커 범위로 옮길 때 이 칸에 그 범위를 얹는다(`SceneSubLevelSubsystem::makeMemberNodes`) — 글 상태는 `_idScope` 가 같은 일을 한다. 파일에는 쓰지 않는다.
             */
            uint64 _cookedStateIDScope{ 0 };
            /**
             * @brief **런타임 칸.** 씬 인스턴스 앵커면 넣은 씬이 쿠킹에서 정적 메시를 묶는지입니다(내부 씬의 `SceneSettings::_packing`, 펼칠 때 정한다 — `expandSceneInstances`).
             * @details 쿠커만 읽는다(`SceneCooker::packStaticMeshes`). 파일에는 쓰지 않는다 — 정하는 곳은 내부 씬 하나다.
             */
            SceneInstancePacking _instancePacking{ SceneInstancePacking::Separate };
            /**
             * @brief **런타임 칸.** 상태에 적힌 자리가 어느 월드 원점 기준인지입니다(절대 = 로컬 + 원점, `Scene::getWorldOrigin`). 씬 파일 · 쿠킹본의 엔티티는 0 이다.
             * @details 떠 둔 상태(`Scene::captureCellNodes`)는 뜬 때의 원점을 적는다. 지을 때 루트 오브젝트를 (이 값 − 지금 원점)만큼 옮겨 지금 원점 기준으로 맞춘다(`Scene::finishNodeBuild`) —
             *          목록이 섞여도(셀에 들어온 엔티티) 엔티티마다 맞는다. 세이브에 싣는다(`writeSavedNodes`).
             */
            double3 _worldOrigin{};
            /** @brief 멤버가 위에서부터 차례로 받은 덮어쓰기(`<PrefabOverrides>`)입니다. 안쪽 인스턴스의 것이 먼저입니다. 프리팹 상태 · 글 상태 위에 차례로 얹습니다. */
            vector<string> _listInstanceOverrideXML;
            /** @brief **저작 칸.** 문서 뿌리에서 멤버까지의 경로입니다(`5/7/3` — 앵커 파일 id 들 / 내부 파일 id). 앞 칸이 이 문서의 앵커이고 나머지가 그 앵커의 `<Part path>` 입니다. */
            string _instancePartPath;
            /** @brief **저작 칸.** 맨 위 앵커의 `<Part>` 가 이 멤버에 적은 것입니다. 안 올린 레이어의 멤버는 살아 있는 값이 없어 저장이 이것을 그대로 다시 적습니다. */
            string _instanceTopOverrideXML;
            /**
             * @brief **런타임 칸.** 글 · 바이너리 상태를 읽을 목적입니다. 씬 파일 · 쿠킹본에서 읽은 엔티티는 `Authoring` 이고 파일에는 쓰지 않습니다.
             * @details 스트리밍이 셀을 내릴 때 떠 둔 엔티티(`Scene::captureCellNodes`)는 `Snapshot` 이다 — 런타임 칸(`RuntimeState`)까지 실려 있어 저작 목적으로 읽으면 로드 오류다.
             *          세이브에 실을 때는 스트리밍이 엔티티마다 따로 적는다(`SceneStreamingSubsystem::writeSaveState`).
             */
            SerializePurpose _statePurpose{ SerializePurpose::Authoring };
        };

        /**
         * @brief 스트리밍하는 씬의 셀 하나와 그 셀에 놓인 엔티티입니다. **쿠킹본에만 있습니다**(`SceneCooker` 가 나눈다).
         * @details 셀의 엔티티는 같은 셀의 엔티티나 기본 목록(`_listSceneObjectNode`, 늘 올라 있다)의 엔티티만 파일 id 로 가리킨다 — 쿠커가 그 밖의 참조를
         *          쿠킹 오류로 멈춘다(`StreamingCellGrid::findCrossCellReferences`).
         */
        struct StreamingCell
        {
            StreamingCellCoord      _cell{};
            vector<SceneObjectNode> _listSceneObjectNode; ///< 쿠커가 메모리에서 나눈 셀의 엔티티. 쿠킹본에서 읽은 셀은 비어 있다 — 내용은 파일의 구간(`_contentOffset` · `_contentSize`)에 남는다
            vector<string>          _listDataLayer;       ///< 이 셀 조각의 데이터 레이어(비면 레이어 없는 엔티티). 언리얼 World Partition 처럼 좌표 + 레이어 집합이 런타임 셀 하나다 — 켜고 끌 때 이 조각만 오르내린다
            uint64                  _contentOffset{ 0 };  ///< 쿠킹본 안 셀 내용(엔티티 목록)의 시작 바이트(`readStreamingCellContent` 로 푼다)
            uint64                  _contentSize{ 0 };    ///< 셀 내용의 바이트 수. 0 이면 내용이 `_listSceneObjectNode` 에 있다(메모리에서 나눈 문서)
            /// 이 조각의 HLOD 대리 엔티티(기본 목록 `_listSceneObjectNode` 에 든 늘 올라 있는 엔티티)의 파일 id — 쿠커가 조각의 정적 메시를 인스턴스 메시로 모아 굽는다
            /// (`SceneCooker::splitStreamingCells`). 0 이면 대리가 없다(정적 메시가 없는 조각). 스트리밍은 조각이 올라 있지 않을 때만 대리를 켠다
            uint64 _hlodFileID{ 0 };

            /** @brief 내용이 쿠킹본의 구간에 있으면(색인만 읽은 셀) true 입니다. */
            bool hasFileContent() const { return _contentSize != 0; }
        };

        string                  _name;
        string                  _sourcePath;
        string                  _baseScenePath;       ///< 씬 상속의 부모(루트의 `_base`). 읽을 때 펼치고 저장이 차이로 쓴다(`saveXMLDelta`). 쿠킹본은 비어 있다
        SceneSettings           _sceneSettings{};     ///< 씬 파일의 `<SceneSettings>`(형식 판 2부터 필수)
        vector<SceneObjectNode> _listSceneObjectNode; ///< 늘 올라 있는 엔티티. 스트리밍하지 않는 씬은 모든 엔티티다
        vector<StreamingCell>   _listStreamingCell;   ///< 셀로 나눈 엔티티(쿠킹본에만, z · x 오름차순). XML 로는 쓰지 않는다
        /**
         * @brief 셀 엔티티(인스턴스 멤버 제외)의 가장 큰 파일 id 입니다 — 쿠킹본 머리에 실린다(`saveBinary` 가 셀 내용에서 세고 `loadBinary` 가 읽는다).
         * @details 색인만 읽은 셀은 내용을 모른다. 씬이 새 오브젝트에 줄 파일 id(`Scene::computeNextFileID`)가 내려간 셀의 엔티티와 겹치지 않게 이 값 위에서 고른다.
         *          메모리에 든 셀(쿠커가 나눈 문서 · 테스트)은 0 이어도 된다 — 스트리밍이 내용에서 다시 센다(`SceneStreamingSubsystem::getMaxCellFileID`).
         */
        uint64 _maxCellFileID{ 0 };
        bool   _bValid{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

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
        [[nodiscard]] SW_API bool saveToFile( const AbsolutePath& absPath ) const;
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
        /**
         * @brief 리소스 id 의 바이너리(SCN1)를 로드합니다(OS 경로는 받지 않습니다). 월드 설정 · 늘 올라 있는 엔티티 · 셀 **색인**(좌표 · 레이어 · 내용 구간)을 읽습니다.
         * @details 셀 내용은 풀지 않는다 — 스트리밍이 셀을 올릴 때 그 구간만 비동기로 읽어 워커에서 푼다(`readStreamingCellContent`, 언리얼 World Partition 의 셀 패키지 비동기 로드).
         */
        [[nodiscard]] SW_API bool loadBinary( const AssetID& assetID );
        /** @brief 바이너리(SCN1)를 디스크 경로에 저장합니다. 월드 설정과 셀까지 씁니다. */
        [[nodiscard]] SW_API bool saveBinary( const AbsolutePath& absPath ) const;
        /**
         * @brief 쿠킹본에서 셀 하나의 구간(@p pData, @p size — `StreamingCell::_contentOffset` · `_contentSize`)을 엔티티 목록으로 풉니다. 문서를 건드리지 않아 워커에서 부릅니다.
         * @return 구간이 엔티티 목록으로 읽히지 않으면(잘림 · 손상) 오류를 남기고 false 입니다. @p sourcePath 는 로그용입니다.
         */
        [[nodiscard]] SW_API static bool readStreamingCellContent( const uint8* pData, uint64 size, vector<SceneObjectNode>& outListNode, string_view sourcePath );
        /** @brief 엔티티 목록을 셀 내용 형식으로 씁니다(`readStreamingCellContent` 의 짝 — 비우고 채운다). 읽을 목적(`_statePurpose`)은 쓰지 않습니다. */
        SW_API static void writeStreamingCellContent( const vector<SceneObjectNode>& listNode, vector<uint8>& outBytes );
        /**
         * @brief 세이브에 싣는 엔티티 목록을 씁니다(비우고 채운다) — 셀 내용 형식에 엔티티마다 런타임 칸(읽을 목적 `_statePurpose` · 쿠킹 상태 범위 · 월드 원점)을 더한다. 떠 둔 상태(스냅숏)와 문서 내용(저작)이 섞여도 제 목적으로 읽힌다.
         * @details 스트리밍 셀(`SceneStreamingSubsystem::writeSaveState`)과 데이터 레이어 묶음(`SceneDataLayerSubsystem::writeSaveState`)이 같은 형식을 쓴다.
         */
        SW_API static void writeSavedNodes( const vector<SceneObjectNode>& listNode, vector<uint8>& outBytes );
        /** @brief `writeSavedNodes` 의 짝입니다. 잘렸거나 목적 수 · 값이 틀리면 오류를 남기고 false 입니다(@p sourcePath 는 로그용). */
        [[nodiscard]] SW_API static bool readSavedNodes( const uint8* pData, uint64 size, vector<SceneObjectNode>& outListNode, string_view sourcePath );
        /** @brief 엔티티 목록에서 인스턴스 멤버(넓은 평평한 id)를 뺀 가장 큰 파일 id 입니다(없으면 0). 셀 머리 값(`_maxCellFileID`)과 씬의 다음 파일 id 가 같은 규칙으로 센다. */
        [[nodiscard]] SW_API static uint64 computeMaxFileID( const vector<SceneObjectNode>& listNode );
        /** @brief 셀까지 메모리에 든 엔티티 수입니다(색인만 읽은 셀의 엔티티는 세지 않는다). */
        SW_API size_t computeTotalEntityCount() const;

    private:
        /**
         * @brief XML 읽기 입구 둘(`loadXML` · `loadXMLFromDiskFile`)이 읽은 문서를 풉니다.
         * @details 리소스 id 와 디스크 경로는 입구가 형식으로 갈라 읽는다 — 안쪽이 열거형으로 고르면 같은 글이 어느 쪽으로도 흘러간다.
         */
        [[nodiscard]] bool readXMLSource( XMLDocument& doc, bool bRead, const string& absPath, const string& flattenedBaseID, AssetLoadScope& loadScope, vector<string>& inoutListChain );
        /** @brief 리소스 id 의 XML 을 읽습니다. @p inoutListChain 은 지금 펼치는 씬 사슬입니다(자기 포함 · 깊이 검사). */
        [[nodiscard]] bool loadXMLChained( const AssetID& assetID, vector<string>& inoutListChain );
        /** @brief 씬 인스턴스 앵커마다 넣은 씬의 엔티티를 앵커 뒤에 펼칩니다(런타임은 이 평평한 목록만 본다). 없는 씬 · 자기 포함 · 겹친 id 는 오류입니다. */
        [[nodiscard]] bool expandSceneInstances( string_view absPath, vector<string>& inoutListChain );
        /** @brief 문서를 XML 로 만듭니다. 읽는 쪽이 받지 않는 모양이면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool buildXML( XMLDocument& outDoc ) const;
    };

} // namespace sw
