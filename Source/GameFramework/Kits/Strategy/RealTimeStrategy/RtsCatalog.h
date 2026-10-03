/**
 * @file RtsCatalog.h
 * @brief RTS 의 데이터 — 유닛 · 건물 · 자원(체력 · 방어 · 속도 · 시야 · 값 · 보급 · 생산 시간 · 공격 · 일꾼 · 본진 · 생산처 · 선행 건물)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 정의의 종류입니다. */
    enum class RtsUnitKind : uint8
    {
        Unit = 0, ///< 움직인다
        Building, ///< 격자 칸을 막는다
        Resource  ///< 광물 · 가스 — 주인이 없다
    };

    /** @brief 자원 종류입니다. */
    enum class RtsResourceType : uint8
    {
        Minerals = 0,
        Gas,
        None
    };

    /** @brief 유닛 · 건물 · 자원 정의 하나입니다. */
    struct RtsUnitDef
    {
        hashed_string   _id{};
        string          _name{};
        hashed_string   _producedBy{}; ///< 만드는 건물 id(건물이면 짓는 일꾼 id)
        hashed_string   _requires{};   ///< 다 지어진 이 건물이 있어야 만든다(테크)
        float32         _hp{ 40.0f };
        float32         _armor{ 0.0f };
        float32         _speed{ 2.8f };
        float32         _radius{ 0.4f };
        float32         _sight{ 8.0f };
        float32         _buildTime{ 15.0f };
        float32         _damage{ 0.0f };
        float32         _range{ 0.3f }; ///< 몸 가장자리 사이 거리
        float32         _cooldown{ 1.0f };
        float32         _gatherTime{ 2.8f };
        int32           _minerals{ 50 };
        int32           _gas{ 0 };
        int32           _supplyCost{ 0 };
        int32           _supplyProvided{ 0 };
        int32           _footprint{ 1 }; ///< 건물 · 자원이 막는 칸(정사각형 한 변)
        int32           _cargo{ 5 };     ///< 일꾼이 한 번에 나르는 양
        int32           _resourceAmount{ 0 };
        RtsUnitKind     _kind{ RtsUnitKind::Unit };
        RtsResourceType _resourceType{ RtsResourceType::None }; ///< 자원 — 무엇을 주는가 · 건물 — 무엇을 뽑아 올리는가(정제소)
        uint8           _bWorker{ SW_FALSE };
        uint8           _bDepot{ SW_FALSE }; ///< 일꾼이 자원을 내려놓는 곳
        uint8           _bAir{ SW_FALSE };
        uint8           _bTargetsGround{ SW_TRUE };
        uint8           _bTargetsAir{ SW_FALSE };
        uint8           _bExtractor{ SW_FALSE }; ///< 가스 간헐천 위에 짓는다

        bool canAttack() const { return _damage > 0.0f; }
        bool isMobile() const { return _kind == RtsUnitKind::Unit; }
    };

    /**
     * @class RtsCatalog
     * @brief `<RtsCatalog supplyMax="200"><Unit id="worker" kind="Unit" hp="45" .../></RtsCatalog>` 를 읽습니다(키는 `Resource/game/starskirmish/data/units.xml`).
     */
    class SW_GF_API RtsCatalog
    {
    public:
        RtsCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        const RtsUnitDef*         findUnit( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<RtsUnitDef>& getUnits() const { return _catalog.getAll(); }
        /** @brief @p producerId 가 만드는(짓는) 정의들입니다(읽은 순서). */
        void  findProducts( const hashed_string& producerId, vector<const RtsUnitDef*>& outListDef ) const;
        int32 getSupplyMax() const { return _supplyMax; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<RtsUnitDef> _catalog;
        int32                   _supplyMax;
    };
} // namespace sw
