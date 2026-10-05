/**
 * @file ScavengerCarry.h
 * @brief 들고 다니는 칸 — 칸 수(기본 4) · 양손 아이템(드는 동안 다른 것을 줍지 못한다) · 무게에 따른 이동 속도 감소입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerCatalog.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerFacility.h"

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
     * @brief 한 사람이 든 것입니다. 이동 속도 배율 = max(최소, 1 − 무게 × 무게당 감소) 입니다.
     */
    class SW_GF_API ScavengerCarry
    {
    public:
        static constexpr uint32 kStateTag     = 0x41434353u; ///< 'SCCA'
        static constexpr uint32 kStateVersion = 1;

        ScavengerCarry();

        void initialize( const ScavengerCarrySettings& settings );
        /** @brief 넣을 수 있는지 봅니다(칸 · 양손). */
        ScavengerPickupResult evaluatePickup() const;
        /** @brief 넣습니다. `evaluatePickup` 이 Ok 가 아니면 넣지 않고 그 결과입니다. */
        ScavengerPickupResult add( const ScavengerScrap& scrap );
        /** @brief 고유 번호로 꺼냅니다. 없으면 false 입니다. */
        [[nodiscard]] bool tryRemove( int32 uid, ScavengerScrap& outScrap );
        /** @brief 모두 꺼내 @p outListScrap 뒤에 붙입니다(내려놓기 · 죽음). */
        void takeAll( vector<ScavengerScrap>& outListScrap );

        float32                       computeWeight() const;
        float32                       computeSpeedScale() const;
        int32                         computeValue() const;
        bool                          isHoldingTwoHanded() const;
        int32                         getCount() const { return static_cast<int32>( _listScrap.size() ); }
        const vector<ScavengerScrap>& getScraps() const { return _listScrap; }

        /** @brief 든 것을 씁니다. 설정(칸 수 · 무게)은 `initialize` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌거나 칸 수를 넘으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        vector<ScavengerScrap> _listScrap;
        ScavengerCarrySettings _settings;
    };
} // namespace sw
