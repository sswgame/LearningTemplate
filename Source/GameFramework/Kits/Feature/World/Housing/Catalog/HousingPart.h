/**
 * @file HousingPart.h
 * @brief 하우징 부품(벽 · 바닥 · 문 · 가구) 카탈로그를 정의합니다(`housingparts.xml`). 부품마다 프리팹 · 점유 칸 · 층(배치 표면) · 스냅 지점 · 비용 · 판(버전)을 가집니다.
 * @details 부품은 격자 칸을 차지한다(RimWorld · 마인크래프트 같은 칸 벽). 심즈의 칸 모서리 벽과 발하임의 자유 스냅 조각은 같은 자리를 다르게 푼다 — 심즈는 모서리 격자,
 *          발하임은 조각마다 스냅 점, 언리얼 Fortnite 건축은 칸 격자 + 조각 종류다(기억에 의존한 비교). 첫 판은 칸 격자 + 부품의 스냅 지점(소켓) 둘을 함께 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /**
     * @brief 부품이 차지하는 층(배치 표면)입니다. 같은 층끼리는 칸을 나누지 않습니다.
     * @details 바닥은 땅(0 층)이나 아래층 구조물 위에, 구조물(벽 · 문)과 가구는 같은 층의 바닥 위에 놓인다. 구조물과 가구는 서로 칸을 나누지 않는다.
     */
    ENUM()
    enum class HousingPartLayer : uint8
    {
        Floor = 0,
        Structure, ///< 벽 · 문. 위층 바닥을 받친다
        Furniture
    };

    /** @brief 부품 하나의 스냅 지점(소켓)입니다. 다른 부품의 `snapTo` 가 이 종류를 가리키면 그 부품이 이 점에 붙습니다. */
    REFLECT()
    struct HousingSocketDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _kind{}; ///< 소켓 종류(`wallEnd` …). 붙는 쪽 부품의 `snapTo` 와 같은 이름이다
        PROPERTY()
        float3 _position{ 0.0f, 0.0f, 0.0f }; ///< 부품 기준 자리(m). 원점은 점유 칸의 가운데 바닥이며 회전 0 일 때의 값이다
        PROPERTY( Min = 0, Max = 3 )
        int32 _quarterTurns{ 0 }; ///< 붙는 부품이 받을 회전(90 도 단위, 이 부품의 회전에 더한다)
    };

    /**
     * @brief 부품 한 종입니다.
     * @details 저장은 부품 id 와 판을 적는다. 판이 오르면 `_minLoadVersion` 이 옛 세이브를 그대로 읽을 수 있는 가장 낮은 판이다 — 그보다 낮은 판의 배치는 불러오기 오류다(조용히 바꾸지 않는다).
     *          지운 부품을 새 부품이 이어받으면 새 부품의 `replaces` 에 옛 id 를 적는다. 불러오기는 옛 id 의 배치를 새 부품의 지금 판으로 읽는다.
     */
    REFLECT()
    struct HousingPartDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Name = "prefab", Required )
        string _prefabPath{}; ///< 회전 0 으로 저작한 프리팹(점유 칸의 가운데 바닥이 원점)
        PROPERTY()
        HousingPartLayer _layer{ HousingPartLayer::Furniture };
        PROPERTY( Min = 1, Max = 16 )
        int32 _sizeX{ 1 }; ///< 회전 0 일 때 X 로 차지하는 칸 수
        PROPERTY( Min = 1, Max = 16 )
        int32 _sizeZ{ 1 }; ///< 회전 0 일 때 Z 로 차지하는 칸 수
        PROPERTY( Min = 0.01 )
        float32 _height{ 1.0f }; ///< 높이(m). 고스트 상자와 소켓 범위 검사가 쓴다
        PROPERTY( Min = 0 )
        int32 _cost{ 0 }; ///< 놓을 때 카탈로그 통화로 내는 값
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _refundRatio{ 1.0f }; ///< 치울 때 돌려받는 비율(내림)
        PROPERTY( Min = 1 )
        int32 _version{ 1 };
        PROPERTY( Min = 1 )
        int32 _minLoadVersion{ 1 }; ///< 이 판 이상으로 저장한 배치만 읽는다(`_version` 이하)
        PROPERTY( Name = "replaces" )
        hashed_string _replacedID{}; ///< 이 부품이 이어받은 지운 부품의 id
        PROPERTY( Name = "snapTo" )
        hashed_string _snapKind{}; ///< 가까이 있으면 붙을 소켓 종류(비면 격자만)
        PROPERTY()
        bool                     _bFreePlacement{ false }; ///< 자유 배치(격자 · 90 도 밖)를 허락한다. 부지도 허락해야 한다
        vector<HousingSocketDef> _listSocket{};            ///< `<Socket>` 자식(손으로 읽는다)
    };
} // namespace sw

namespace sw
{
    /** @brief `<HousingPartCatalog>` 루트 속성의 스키마입니다. 칸 크기와 층 높이는 부품이 그 크기로 저작됐으므로 카탈로그가 정본이다(부지가 따로 들지 않는다). */
    REFLECT()
    struct HousingPartCatalogRootDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _currency{};
        PROPERTY( Min = 0.1 )
        float32 _cellSize{ 1.0f }; ///< 칸 한 변(m)
        PROPERTY( Min = 0.1 )
        float32 _levelHeight{ 3.0f }; ///< 층 높이(m)
        PROPERTY( Min = 0.0 )
        float32 _socketSnapRadius{ 0.5f }; ///< 소켓에 붙는 거리(m)
    };
} // namespace sw

namespace sw
{
    /**
     * @class HousingPartCatalog
     * @brief 하우징 부품 카탈로그입니다. 아래 형식의 XML 을 읽습니다.
     *        `<HousingPartCatalog currency="Gold" cellSize="1" levelHeight="3"><Part id="wall" name="Wall" prefab="…" layer="Structure" cost="10">
     *        <Socket kind="wallEnd" position="0.5,0,0"/></Part></HousingPartCatalog>`
     * @details 읽을 때 스냅 지점을 검사한다(에디터 저작 검증의 정본): 소켓은 부품의 점유 상자 안이어야 하고, `snapTo` 는 이 카탈로그의 어떤 소켓 종류여야 하며,
     *          `minLoadVersion` 은 `version` 이하, `replaces` 는 지금 카탈로그에 없는 id 여야 한다. 틀린 부품은 넣지 않고 오류다.
     */
    class SW_KIT_API HousingPartCatalog : public XMLCatalog<HousingPartCatalog>
    {
        friend class XMLCatalog<HousingPartCatalog>;

    public:
        /** @brief 이 데이터 종류입니다(등록은 .cpp 의 공유 캐시 줄이 로더를 더한다). 접미사가 없어 타입 있는 참조 `AssetRef<HousingPartCatalog>` 는 경로 규칙으로 가른다. */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( "housingparts", AssetPathRule{ "game/", {}, "/data/housingparts.xml" }, {} );

        HousingPartCatalog();

        /** @brief 부품을 더합니다. 겹친 id 는 거절합니다(false). 테스트와 코드로 짓는 쪽이 씁니다. 스냅 지점 검사는 `loadRoot` 가 모두 읽은 뒤에 합니다. */
        [[nodiscard]] bool addPart( const HousingPartDef& part );
        /** @brief 루트 값을 정합니다(코드로 짓는 쪽과 테스트). */
        void setRoot( const HousingPartCatalogRootDef& root ) { _root = root; }

        const HousingPartDef* findPart( const hashed_string& partID ) const { return _catalog.find( partID ); }
        /** @brief @p removedID 를 `replaces` 로 이어받은 부품입니다. 없으면 nullptr 입니다. */
        const HousingPartDef* findReplacement( const hashed_string& removedID ) const;
        /** @brief 부품의 스냅 지점과 판을 봅니다. 문제마다 @p outListProblem 에 한 줄씩 더하고 문제가 없으면 true 입니다(카탈로그 읽기와 부지 검증이 쓴다). */
        bool checkPart( const HousingPartDef& part, vector<string>& outListProblem ) const;

        const vector<HousingPartDef>&    getParts() const { return _catalog.getAll(); }
        const HousingPartCatalogRootDef& getRoot() const { return _root; }

    private:
        static constexpr const utf8* kXMLRootName = "HousingPartCatalog"; ///< 루트 요소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        /** @brief `<Part>` 하나와 `<Socket>` 자식을 읽습니다. 오류면 넣지 않고 false 입니다. */
        [[nodiscard]] bool loadPart( const XMLNode& node, string_view sourceName );

    private:
        GameCatalog<HousingPartDef> _catalog;
        HousingPartCatalogRootDef   _root;
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 `SharedAssetCache<HousingPartCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, HousingPartCatalog );
} // namespace sw
