/**
 * @file RTSCatalog.h
 * @brief RTS 의 데이터를 담습니다. 유닛, 건물, 자원마다 체력, 방어, 속도, 시야, 비용, 보급, 생산 시간, 공격, 일꾼 여부, 본진 여부, 생산처, 선행 건물을 가집니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /** @brief 정의의 종류입니다. */
    ENUM()
    enum class RTSUnitKind : uint8
    {
        Unit = 0, ///< 움직이는 정의이다
        Building, ///< 격자 셀을 막는다
        Resource  ///< 광물이나 가스이며 주인이 없다
    };

    /** @brief 자원의 종류입니다. */
    ENUM()
    enum class RTSResourceKind : uint8
    {
        Minerals = 0,
        Gas,
        None
    };

    /** @brief 유닛, 건물, 자원 정의 하나입니다. */
    REFLECT()
    struct RTSUnitDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY()
        hashed_string _producedBy{}; ///< 만드는 건물 id 이다(건물이면 짓는 일꾼 id)
        PROPERTY()
        hashed_string _requires{}; ///< 건설이 끝난 이 건물이 있어야 만들 수 있다(테크)
        PROPERTY( Min = 1.0 )
        float32 _hp{ 40.0f };
        PROPERTY( Min = 0.0 )
        float32 _armor{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _speed{ 0.0f }; ///< 이동 속도(m/s)이다. 적지 않으면 움직이지 않는다(건물 · 자원)
        PROPERTY( Min = 0.1 )
        float32 _radius{ 0.4f };
        PROPERTY( Min = 0.0 )
        float32 _sight{ 8.0f };
        PROPERTY( Min = 0.1 )
        float32 _buildTime{ 15.0f };
        PROPERTY( Min = 0.0 )
        float32 _damage{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _range{ 0.3f }; ///< 몸 가장자리 사이의 거리
        PROPERTY( Min = 0.05 )
        float32 _cooldown{ 1.0f };
        PROPERTY( Min = 0.1 )
        float32 _gatherTime{ 2.8f };
        PROPERTY( Min = 0 )
        int32 _minerals{ 50 };
        PROPERTY( Min = 0 )
        int32 _gas{ 0 };
        PROPERTY( Name = "supply", Min = 0 )
        int32 _supplyCost{ 0 };
        PROPERTY( Name = "provides", Min = 0 )
        int32 _supplyProvided{ 0 };
        PROPERTY( Min = 1, Max = 8 )
        int32 _footprint{ 1 }; ///< 건물과 자원이 막는 셀 수(정사각형의 한 변)
        PROPERTY( Min = 1 )
        int32 _cargo{ 5 }; ///< 일꾼이 한 번에 나르는 양
        PROPERTY( Name = "amount", Min = 0 )
        int32 _resourceAmount{ 0 };
        PROPERTY()
        RTSUnitKind _kind{ RTSUnitKind::Unit };
        PROPERTY( Name = "resource" )
        RTSResourceKind _resourceType{ RTSResourceKind::None }; ///< 자원이면 무엇을 주는지, 건물(정제소)이면 무엇을 뽑아 올리는지를 나타낸다
        PROPERTY()
        bool _bWorker{ false };
        PROPERTY()
        bool _bDepot{ false }; ///< 일꾼이 자원을 내려놓는 곳이다
        PROPERTY()
        bool _bAir{ false };
        bool _bTargetsGround{ true };
        bool _bTargetsAir{ false };
        PROPERTY()
        bool _bExtractor{ false }; ///< 가스 간헐천 위에 짓는다

        bool canAttack() const { return _damage > 0.0f; }
        bool isMobile() const { return _kind == RTSUnitKind::Unit; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 규칙의 수치입니다. 시간 단위는 게임 초입니다.
     * @details 규칙 자산이다 — 유닛 카탈로그의 `<Rules …/>` 한 줄이 모든 칸을 적는다(`Required`, 없으면 카탈로그 로드가 실패한다). 월드는 카탈로그에서만 받으므로
     *          게임이 C++ 기본값(`RTSSettings{}`)으로 판을 열 길이 없다(감사 K-4, Lyra `ULyraExperienceDefinition` · 유니티 `ScriptableObject` 와 같은 자리).
     *          범위는 필드의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 읽기가 그 줄을 넣지 않는 오류로 거절한다. 값을 조용히 맞추지 않는다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API RTSSettings
    {
        REFLECT_BODY();
        PROPERTY( Required, Min = 0.001, Max = 1.0 )
        float32 _fixedStep{}; ///< 시뮬레이션 한 걸음의 길이(20 Hz, 스타크래프트의 게임 틱과 비슷하다)
        PROPERTY( Required, Min = 0.0 )
        float32 _minimumDamage{}; ///< 방어가 높아도 최소한 이만큼은 피해가 들어간다
        PROPERTY( Required, Min = 0.0 )
        float32 _repathInterval{}; ///< 움직이는 목표를 쫓을 때 경로를 다시 구하는 간격
        PROPERTY( Required, Min = 0.0 )
        float32 _visionInterval{}; ///< 안개를 다시 칠하는 간격
        PROPERTY( Required, Min = 0.0 )
        float32 _interactSlack{}; ///< 채취, 건설, 반납에서 닿았다고 보는 가장자리 거리
        PROPERTY( Required, Min = 0.0 )
        float32 _resourceSearchRadius{}; ///< 광물이 바닥났을 때 대신할 광물을 찾는 반경
        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _constructionStartRatio{}; ///< 건설을 시작한 건물이 처음 가지는 체력 비율
        PROPERTY( Required, Min = 0.0 )
        float32 _underAttackCooldown{}; ///< "공격받고 있다" 알림의 간격(플레이어마다 적용)
        PROPERTY( Required, Min = 0.0 )
        float32 _autoTargetLeashScale{}; ///< 자동으로 고른 목표가 시야의 이 배율보다 멀어지면 놓는다(명령한 목표는 끝까지 쫓는다)
        PROPERTY( Required, Min = 1 )
        int32 _productionQueueMax{};
        PROPERTY( Required, Min = 1 )
        int32 _flowFieldGroupSize{}; ///< 이 수 이상을 한 곳으로 보내면 A* 대신 흐름장 하나를 쓴다
        PROPERTY( Required, Min = 1 )
        int32 _bucketSize{}; ///< 이웃 탐색 버킷의 한 변 길이(셀 수)
        PROPERTY( Required )
        hashed_string _mineralCurrency{}; ///< 플레이어 지갑에서 광물로 쓰는 통화
        PROPERTY( Required )
        hashed_string _gasCurrency{}; ///< 플레이어 지갑에서 가스로 쓰는 통화
    };

} // namespace sw

namespace sw
{
    /** @brief `<RTSCatalog>` 루트 속성의 스키마입니다. */
    REFLECT()
    struct RTSCatalogRootDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 1 )
        int32 _supplyMax{ 200 }; ///< 보급 상한
    };
} // namespace sw

namespace sw
{
    /**
     * @class RTSCatalog
     * @brief `<RTSCatalog supplyMax="200"><Rules fixedStep="0.05" .../><Unit id="worker" kind="Unit" hp="45" .../></RTSCatalog>` 를 읽습니다. `<Rules>` 는 꼭 한 줄이다. 키는 `Content/game/playground/starskirmish/data/units.xml` 입니다.
     */
    class SW_KIT_API RTSCatalog : public GameCatalog<RTSUnitDef, XMLCatalog<RTSCatalog>>
    {
        friend class XMLCatalog<RTSCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_ASSET_TYPE` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "rtsunits";
        /** @brief 이 데이터 종류입니다(등록은 `RTSCatalog.cpp` 의 공유 캐시 줄이 로더를 더한다). 접미사가 없어 타입 있는 참조 `AssetRef<RTSCatalog>` 는 경로 규칙으로 가른다. */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( kDataKind, AssetPathRule{ "game/", {}, "/data/units.xml" }, {} );

        RTSCatalog();

        /** @brief @p producerID 가 만드는(짓는) 정의를 읽은 순서대로 모읍니다. */
        void  findProducts( const hashed_string& producerID, vector<const RTSUnitDef*>& outListDef ) const;
        int32 getSupplyMax() const { return _supplyMax; }
        /** @brief 규칙(`<Rules>`)입니다. 로드에 성공한 카탈로그는 반드시 한 줄을 가진다. */
        const RTSSettings& getRules() const { return _rules; }

    private:
        static constexpr const utf8* kXMLRootName = "RTSCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        /** @brief `<Unit>` 하나를 스키마로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadUnit( const XMLNode& node, string_view sourceName );
        /** @brief `<Rules>` 한 줄을 읽습니다. 칸이 빠졌거나 범위 밖이거나 두 번째 줄이면 false 입니다. */
        [[nodiscard]] bool loadRules( const XMLNode& node, string_view sourceName );

    private:
        RTSSettings _rules;
        int32       _supplyMax;
        int32       _rulesCount; ///< 이번 로드에서 읽은 `<Rules>` 줄 수(1 이어야 한다)
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<RTSCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, RTSCatalog );
} // namespace sw
