/**
 * @file PrefabAsset.h
 * @brief 프리팹 에셋입니다(GameObject 템플릿) — 형식 셋(XML · JSON · PFB2 바이너리)의 읽기 · 쓰기와 중첩 · 변형 표현.
 * @details 로드 캐시와 스폰은 `PrefabCache.h` 다(언리얼이 에셋 직렬화와 `SpawnActor` 를 가르는 나눔).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 프리팹 루트(`<Prefab formatVersion name>`)의 칸입니다. `formatVersion` 은 에셋 형식이 읽는다. */
    REFLECT()
    struct SW_API PrefabRootParams
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
    };

    /** @brief 중첩 프리팹(`<PrefabInstance name prefab attach>`)의 칸입니다. 덮어쓰기와 그 노드에 붙는 노드는 자식 원소다. */
    REFLECT()
    struct SW_API PrefabInstanceParams
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY( AssetPath )
        string _prefab{};
        PROPERTY()
        string _attach{};
    };

    struct CookJobDesc;
    struct ObjectIdentity;

    class AssetLoadScope;
    class CookStore;
    class GameObject;
    class GameObjectManager;
    class JSONDocument;
    class PrefabCache;
    class XMLDocument;

    /// @brief 프리팹 상태 본문의 형식입니다. 읽을 때 한 번 정하고, 상태를 쓰는 쪽은 이 값을 봅니다(본문 첫 글자로 다시 짐작하지 않습니다).
    enum class PrefabStateFormat : uint8
    {
        XML,
        JSON,
    };

    /**
     * @struct PrefabInstanceNode
     * @brief 프리팹 안에 놓인 다른 프리팹의 인스턴스 하나입니다(중첩 프리팹). 스폰하면 자식 오브젝트 하나가 되어 부모 오브젝트의 컴포넌트에 붙습니다.
     * @details 유니티 Nested Prefab 과 같은 모델이다. 원본 경로와 원본과 다른 점(`PrefabOverrides` 형식)만 적으므로 원본을 고치면 다음 스폰부터
     *          모든 상위 프리팹에 퍼진다. 쿠킹본은 오버라이드를 얹은 최종 상태(`_resolvedStateXML`)를 싣고 원본을 읽지 않는다(`PrefabCache::flattenNestedInstances`).
     */
    struct PrefabInstanceNode
    {
        string _name;             ///< 스폰한 오브젝트의 이름. 비면 원본 프리팹의 이름
        string _prefabPath;       ///< 원본 프리팹 경로
        string _attachKey;        ///< 부모 오브젝트에서 붙을 씬 컴포넌트의 키(`이름#n`, `ComponentStableKey`). 비면 부모의 primary 씬 컴포넌트
        string _overrideXML;      ///< 원본과 다른 점(`<PrefabOverrides>`). 비면 원본 그대로
        string _resolvedStateXML; ///< 쿠킹본만: 오버라이드를 얹은 최종 오브젝트 상태. 있으면 원본을 읽지 않고, 원본의 중첩 인스턴스는 뒤 노드로 펼쳐져 있다
        string _partPath;         ///< 쿠킹본만: 펼치기 전 이 노드의 부품 경로(`2/0`, `PrefabPartOverride`). 저작본 노드의 경로는 목록 인덱스다
        /** @brief 부모가 없는 노드(이 프리팹의 루트 오브젝트에 붙는다)의 `_parentIndex` 입니다. */
        static constexpr int32 kRootParent = -1;

        int32 _parentIndex{ kRootParent }; ///< 부모 노드의 목록 인덱스(자기보다 앞). `kRootParent` 면 이 프리팹의 루트 오브젝트

        /** @brief 쿠킹본 형식(펼친 상태)이면 true 입니다. */
        bool isResolved() const { return _resolvedStateXML.empty() == false; }
        /** @brief 이 노드가 스폰한 부품의 키입니다(`PrefabNestedPart::_partKey`) — 펼친 노드는 `_partPath`, 저작본 노드는 목록 인덱스 @p index 입니다. */
        string makePartKey( size_t index ) const;
    };
} // namespace sw

namespace sw
{
    /// @brief 프리팹 에셋입니다(루트 GameObject 상태 템플릿과 그 안에 놓인 중첩 프리팹 인스턴스).
    class SW_API PrefabAsset
    {
    public:
        /** @brief 프리팹 파일의 접미사입니다 — 저작 소스 둘(XML · JSON), 쿠킹본, 확장자 없는 이름(쿠킹본으로 읽는다). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".prefab.xml",  AssetSuffixRole::CookSource},
            {".prefab.json",  AssetSuffixRole::CookSource},
            { ".prefab.bin",      AssetSuffixRole::Cooked},
            {     ".prefab", AssetSuffixRole::CookedAlias},
        };
        /** @brief 프리팹 에셋 종류입니다(등록은 `PrefabAsset.cpp` 의 `SW_ASSET_TYPE`, 캐시 등록은 `PrefabCache.cpp`). 판 0. */
        static constexpr AssetTypeDef kAssetType{ "Prefab", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), ".prefab.bin", 0, {} };

        /** @brief 로드하지 않은 빈 프리팹으로 만듭니다. */
        PrefabAsset();

        /**
         * @brief XML 에서 프리팹을 로드합니다(`<Prefab formatVersion="0" name="..."><GameObject …/><PrefabInstance …/></Prefab>`. 저장하는 형태 하나만 받습니다).
         * @details 중첩 인스턴스는 `<PrefabInstance name prefab attach>` 이고 안에 `<PrefabOverrides>` 와 그 인스턴스에 붙는 `<PrefabInstance>` 를 둔다.
         */
        [[nodiscard]] bool loadFromXMLFile( const AssetID& assetID );
        /** @brief JSON 에서 프리팹을 로드합니다(GameObject 상태 JSON 에 중첩 인스턴스 배열 `_listPrefabInstance` 를 더한 것이며, `saveToJSONFile` 이 쓰는 형태 하나만 받습니다). */
        [[nodiscard]] bool loadFromJSONFile( const AssetID& assetID );
        /**
         * @brief **디스크 파일**에서 프리팹 원본을 로드합니다(형식은 확장자 `.prefab.xml` · `.prefab.json`). 리소스 id 로 풀지 않고 팩을 보지 않습니다.
         * @details 소스 트리를 디스크로 훑은 쿠커 · 테스트용입니다 — 리소스 루트 안 절대 경로를 `loadFromXMLFile` 에 넣으면 팩 전용 구성이 거절합니다.
         *          게임 런타임 코드는 쓰지 않습니다.
         */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief 바이너리에서 프리팹을 로드합니다(쿠킹된 PFB2). 다른 버전의 쿠킹본은 거절합니다(다시 쿠킹합니다). */
        [[nodiscard]] bool loadFromBinaryFile( const AssetID& assetID );
        /**
         * @brief XML 로 저장합니다(<Prefab formatVersion="0" name="...">). 쿠킹본 형식(펼친 중첩 인스턴스)은 저작 파일로 쓸 수 없어 false 입니다.
         * @details 형식 기본값과 같은 칸은 쓰지 않습니다(`PrefabOverrides::omitDefaultValues`). 변형(`getBasePath` 가 있음)은 부모와 다른 점만 `<PrefabOverrides>` 로 씁니다.
         */
        [[nodiscard]] bool saveToXMLFile( const AbsolutePath& absPath ) const;
        /** @brief JSON 으로 저장합니다(GameObject 상태 JSON + `_listPrefabInstance`). 상태를 JSON 으로 옮기지 못하거나 쿠킹본 형식이면 쓰지 않고 오류를 알리며 false 입니다. */
        [[nodiscard]] bool saveToJSONFile( const AbsolutePath& absPath ) const;
        /**
         * @brief 경로의 확장자로 형식을 골라 저장합니다(`.prefab.xml`, `.prefab.json`). 프리팹 경로가 아니면 쓰지 않고 false 입니다.
         * @details 에디터가 프리팹을 쓰는 경로(Apply to Prefab, 격리 편집 저장)는 이 함수만 쓴다. 로더와 쿠커와 같은 규칙이다.
         */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /** @brief Shipping 쿠킹용 PFB2 바이너리로 저장합니다(magic + version + name + 상태 데이터 + 중첩 인스턴스 노드). */
        [[nodiscard]] bool saveToBinaryFile( const AbsolutePath& absPath ) const;
        /**
         * @brief GameObject 상태에서 프리팹을 채웁니다. 그 오브젝트가 스폰한 중첩 프리팹 부품(`PrefabInstanceRegistry`)은 중첩 인스턴스 노드가 됩니다.
         * @details 부품마다 원본 프리팹과 다른 점을 오버라이드로 적는다(에디터의 Apply to Prefab 이 부품의 수정을 바깥 프리팹에 쓰는 경로). 부품이 아닌
         *          자식 오브젝트는 프리팹에 실리지 않는다 — 프리팹은 루트 오브젝트 하나와 중첩 인스턴스로만 이뤄진다.
         * @param pNestedPrefabs 부품의 원본 프리팹을 읽을 캐시입니다. nullptr 이면 엔진의 프리팹 캐시이고, 엔진이 없으면 부품을 싣지 못하고 경고합니다.
         */
        void setFromGameObject( const GameObject* pGameObject, PrefabCache* pNestedPrefabs = nullptr );

        /** @brief 프리팹 이름을 반환합니다. */
        const string& getName() const { return _name; }
        /**
         * @brief 이 프리팹이 변형이면 부모 프리팹 경로(파일의 `_base`)입니다. 아니면 빈 글입니다. 쿠킹본은 펼쳐져 있어 늘 빈 글입니다.
         * @details 변형은 유니티 Prefab Variant 와 같은 모델이다. 파일은 부모 경로와 다른 점(`<PrefabOverrides>`, 씬 인스턴스와 같은 형식)만 적고, 읽으면 `getStateData` · 중첩 인스턴스는 부모를 합친 값이다 —
         *          스폰 · 씬 복원 · 쿠킹은 변형인지 모른다. 부모를 고치면 적지 않은 칸이 따라간다. 변형은 물려받은 중첩 인스턴스를 지울 수 없다(유니티도 같다).
         */
        const string& getBasePath() const { return _basePath; }
        /** @brief 저장을 변형으로 할지 정합니다(에디터가 `setFromGameObject` 뒤에 파일의 부모를 되돌려 놓는다 — `readBasePathFromSource`). */
        void setBasePath( string_view basePath ) { _basePath = string{ basePath }; }
        /** @brief 이름과 XML 상태만 채운 자산으로 만듭니다(저장이 기본값을 뺀 글이 같게 읽히는지 맞대는 데 쓴다). 상태가 비면 false 입니다. */
        [[nodiscard]] bool setStateForCompare( string_view name, string_view stateXML );
        /** @brief 파일 글(XML · JSON)에 적힌 부모 경로입니다. 없으면 빈 글입니다. */
        [[nodiscard]] static string readBasePathFromSource( string_view sourceText );
        /** @brief 변형 파일이 적은 다른 점입니다(`PrefabCache::findFieldOrigin`). 변형이 아니면 빈 글입니다. */
        const string& getVariantOverrides() const { return _variantOverrideXML; }
        /** @brief 변형 사슬이 물려받은 중첩 부품에 덮어쓴 것입니다(`<Part>`, 경로는 이 프리팹 루트에서). 스폰과 펼치기가 노드 위에 얹습니다. */
        const vector<PrefabPartOverride>& getVariantPartOverrides() const { return _listVariantPart; }
        /** @brief 직렬화된 본문 상태 데이터(XML 또는 JSON. `getStateFormat` 참고)를 반환합니다. */
        const string& getStateData() const { return _stateData; }
        /** @brief 상태 본문의 형식입니다(읽을 때 정해집니다). */
        PrefabStateFormat getStateFormat() const { return _stateFormat; }
        /**
         * @brief 프리팹 상태를 오브젝트에 읽어 넣습니다. 오브젝트의 컴포넌트는 모두 다시 만들어집니다(오브젝트는 매니저에 속해야 합니다).
         * @details 스폰, 되돌리기, 오버라이드 비교(CDO), 형식 변환이 모두 이 함수를 쓴다. 형식 판정은 여기 한 곳에서 한다.
         *          다른 오브젝트로의 부착은 읽지 않는다. 프리팹 루트에는 부모가 없다(본문에 남아 있어도 읽지 않는다).
         * @param pIdentity 있으면 다시 만드는 컴포넌트가 그 id 를 되찾습니다(되돌리기에서 인스턴스의 컴포넌트를 가리키던 핸들이 이어집니다).
         */
        [[nodiscard]] bool applyStateTo( GameObject* pTarget, const ObjectIdentity* pIdentity = nullptr ) const;
        /** @brief 로드에 성공했으면 true 입니다. */
        bool isValid() const { return _bValid == SW_TRUE; }
        /**
         * @brief 이 프리팹을 스폰하기 전에 로드해 둘 프리팹 경로를 수집합니다(비우고 채우며, 같은 경로는 한 번).
         * @details 중첩 인스턴스의 원본(펼친 쿠킹본은 원본을 읽지 않으므로 빠진다)과, 상태 · 오버라이드 · 펼친 상태 안에 적힌 `.prefab` 경로(런타임에 스폰하는
         *          프로퍼티)다. 프리로드 수집은 이 목록을 그대로 쓰고, 목록의 프리팹마다 다시 불러 닫힌 집합을 만든다.
         */
        void collectReferencedPrefabPaths( vector<string>& outListPath ) const;

        /** @brief 이 프리팹 안에 놓인 다른 프리팹의 인스턴스입니다. 부모 노드가 자식 노드보다 앞에 옵니다. */
        const vector<PrefabInstanceNode>& getNestedInstances() const { return _listNestedInstance; }
        /**
         * @brief 중첩 인스턴스를 바꿉니다. 부모 인덱스가 자기보다 앞이 아니거나 원본 경로도 펼친 상태도 없는 노드가 있으면 바꾸지 않고 false 입니다.
         * @details 저작 형식(원본 경로 + 오버라이드)과 쿠킹본 형식(펼친 상태)을 섞을 수 있다. 펼친 노드는 원본의 중첩 인스턴스를 뒤 노드로 이미 가지고 있다.
         */
        [[nodiscard]] bool setNestedInstances( vector<PrefabInstanceNode> listNode );
        /** @brief 펼친(쿠킹본 형식) 중첩 인스턴스가 하나라도 있으면 true 입니다. 이런 프리팹은 저작 파일(XML · JSON)로 쓸 수 없습니다. */
        bool hasResolvedNestedInstances() const;

    private:
        /**
         * @brief XML 원본 읽기 입구 둘(`loadFromXMLFile` · `loadFromDiskFile`)이 읽은 문서를 풉니다.
         * @details 리소스 id 와 디스크 경로는 입구가 형식으로 갈라 읽는다 — 안쪽이 열거형으로 고르면 같은 `string_view` 가 어느 쪽으로도 흘러간다.
         * @param bRead 입구의 읽기 결과입니다. @param absPath 로그에 찍는 경로(리소스면 푼 절대 경로)입니다.
         */
        [[nodiscard]] bool readXMLSource( XMLDocument& doc, bool bRead, const string& absPath, AssetLoadScope& loadScope );
        /** @brief JSON 원본 읽기 입구 둘(`loadFromJSONFile` · `loadFromDiskFile`)이 읽은 문서를 풉니다. */
        [[nodiscard]] bool readJSONSource( JSONDocument& doc, bool bRead, const string& absPath );

        /**
         * @brief 상태를 다른 형식의 텍스트로 옮깁니다(XML ↔ JSON). 매니저가 있는 임시 오브젝트를 거칩니다.
         * @details 매니저 없는 `GameObject` 를 거치면 컴포넌트를 만들 팩토리가 없어 **컴포넌트를 모두 버린** 본문이 나온다.
         */
        string convertState( PrefabStateFormat targetFormat ) const;

        /**
         * @brief 변형을 읽습니다 — 부모를 읽어 @p overrideXML 을 얹은 상태와 부모 노드 + @p listOwnNode 를 이 자산에 채웁니다. 순환 · 없는 부모 · 없는 대상 · 깊이 초과는 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool resolveVariant( string_view basePath, string_view overrideXML, vector<PrefabInstanceNode> listOwnNode, string_view sourceName );
        /** @brief 변형을 파일에 쓸 값을 만듭니다 — 부모와 다른 점과 물려받지 않은 노드입니다. 부모를 읽지 못하거나 물려받은 노드를 지웠거나 옮겼으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool makeVariantFileFields( string& outOverrideXML, vector<PrefabInstanceNode>& outListOwnNode ) const;

    private:
        string                     _name;
        string                     _stateData;
        string                     _basePath;           ///< 변형의 부모 프리팹 경로(`getBasePath`)
        string                     _variantOverrideXML; ///< 변형 파일이 적은 다른 점(`getVariantOverrides`)
        vector<PrefabInstanceNode> _listNestedInstance;
        vector<PrefabPartOverride> _listVariantPart; ///< 변형 사슬이 물려받은 부품에 덮어쓴 것(`getVariantPartOverrides`)
        PrefabStateFormat          _stateFormat;
        // `_bValid` 라는 이름은 이 저장소에서 **두 가지**다. 여기와 `RenderFramePacket` 은
        // `uint8 : 1` 비트필드이고, `EditorWorkspace` 와 `SceneDocument` 는 진짜 `bool` 이다.
        // `Style/BitfieldBoolean` 린트는 이름으로만 판정하므로 그런 이름은 **일부러 건너뛴다**
        // (오탐보다 누락이 낫다는 판단). 그래서 여기서는 `SW_TRUE`/`SW_FALSE` 를 손으로 지킨다.
        uint8                  _bValid   : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw

namespace sw
{
    class PrefabLoadOperation;

    /** @brief 프리팹 로드 핸들입니다. 핸들이 살아 있는 동안 프리팹도 살아 있습니다(캐시가 버려도 — 다시 읽기 · `clear`). 로드 작업은 `PrefabCache.h` 에 있습니다. */
    using PrefabHandle = AssetHandle<PrefabLoadOperation>;
} // namespace sw

namespace sw
{
    /** @brief `AssetRef<PrefabAsset>` 은 프리팹 캐시로 간다(스폰이 캐시에 묶여 있어 공유 캐시와 합치지 않았다). */
    template <>
    struct SW_API AssetRefCache<PrefabAsset>
    {
        static shared_ptr<const PrefabAsset> acquire( string_view path );
        static PrefabHandle                  request( string_view path );
    };
} // namespace sw
