/**
 * @file ScavengerCarry.h
 * @brief 들고 다니는 슬롯을 구현합니다. 슬롯 수(기본 4), 양손 아이템(드는 동안 다른 것을 줍지 못한다), 무게에 따른 이동 속도 감소를 다룹니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Rule/ScavengerFacility.h"

namespace sw
{
    class Archive;

    /** @brief 줍기 결과입니다. */
    enum class ScavengerPickupResult : uint8
    {
        Ok = 0,
        SlotsFull,
        HandsFull,  ///< 양손 아이템을 들고 있다
        NotFound,   ///< 그 방에 없다
        Unavailable ///< 죽었거나 내리지 않았다
    };

    /**
     * @class ScavengerCarry
     * @brief 한 사람이 든 것을 관리합니다. 이동 속도 배율은 max(최소, 1 - 무게 x 무게당 감소)입니다.
     */
    class SW_KIT_API ScavengerCarry
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "SCCA" );
        static constexpr uint32 kStateVersion = 1;

        ScavengerCarry();

        void initialize( const ScavengerCarrySettings& settings );
        /** @brief 넣을 수 있는지 확인합니다(슬롯 수와 양손 여부). */
        ScavengerPickupResult evaluatePickup() const;
        /** @brief 넣습니다. `evaluatePickup` 이 Ok 가 아니면 넣지 않고 그 결과를 반환합니다. */
        ScavengerPickupResult add( const ScavengerScrap& scrap );
        /** @brief 고유 번호로 꺼냅니다. 없으면 false 를 반환합니다. */
        [[nodiscard]] bool tryRemove( int32 uid, ScavengerScrap& outScrap );
        /** @brief 모두 꺼내 @p outListScrap 뒤에 붙입니다(내려놓을 때와 죽었을 때 씁니다). */
        void takeAll( vector<ScavengerScrap>& outListScrap );

        float32                       computeWeight() const;
        float32                       computeSpeedScale() const;
        int32                         computeValue() const;
        bool                          isHoldingTwoHanded() const;
        int32                         getCount() const { return static_cast<int32>( _listScrap.size() ); }
        const vector<ScavengerScrap>& getScraps() const { return _listScrap; }

        /** @brief 든 것을 씁니다. 설정(슬롯 수와 무게)은 `initialize` 에서 받으므로 저장하지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌거나 슬롯 수를 넘으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        vector<ScavengerScrap> _listScrap;
        ScavengerCarrySettings _settings;
    };
} // namespace sw
