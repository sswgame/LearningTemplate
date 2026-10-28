/**
 * @file RTSCatalog.h
 * @brief RTS 의 데이터를 담습니다. 유닛, 건물, 자원마다 체력, 방어, 속도, 시야, 비용, 보급, 생산 시간, 공격, 일꾼 여부, 본진 여부, 생산처, 선행 건물을 가집니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

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
    enum class RTSResourceType : uint8
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
        RTSResourceType _resourceType{ RTSResourceType::None }; ///< 자원이면 무엇을 주는지, 건물(정제소)이면 무엇을 뽑아 올리는지를 나타낸다
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
     * @brief `<RTSCatalog supplyMax="200"><Unit id="worker" kind="Unit" hp="45" .../></RTSCatalog>` 를 읽습니다. 키는 `Content/game/starskirmish/data/units.xml` 입니다.
     */
    class SW_KIT_API RTSCatalog : public GameCatalog<RTSUnitDef, XMLCatalog<RTSCatalog>>
    {
        friend class XMLCatalog<RTSCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "rtsunits";

        RTSCatalog();

        /** @brief @p producerID 가 만드는(짓는) 정의를 읽은 순서대로 모읍니다. */
        void  findProducts( const hashed_string& producerID, vector<const RTSUnitDef*>& outListDef ) const;
        int32 getSupplyMax() const { return _supplyMax; }

    private:
        static constexpr const utf8* kXMLRootName = "RTSCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Unit>` 하나를 스키마로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadUnit( const XMLNode& node, string_view sourceName );

        int32 _supplyMax;
    };
} // namespace sw
