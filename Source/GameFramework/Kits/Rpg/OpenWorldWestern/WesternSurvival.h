/**
 * @file WesternSurvival.h
 * @brief 플레이어 코어 — 체력 · 스태미나 · 데드아이 게이지(기반 `ResourceGauge`)와 그 코어, 음식 · 날씨(기반 `WeatherSystem` 의 온도)와 옷에 따른 코어 감소,
 *        데드아이(시간 둔화 배율 · 표시 개수)입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Combat/ResourceGauge.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class WeatherSystem;
    class WesternCatalog;

    /** @brief 플레이어의 코어 종류입니다. */
    enum class WesternCore : uint8
    {
        Health = 0,
        Stamina,
        DeadEye,
        Count
    };

    /**
     * @class WesternSurvival
     * @brief 코어는 0..100 입니다. 게임 시간이 흐르면 모든 코어가 줄고, 체감 온도(날씨 온도 + 옷 보온)가 편한 범위보다 낮으면 체력 코어,
     *        높으면 스태미나 코어가 벗어난 만큼 더 줍니다. 게이지 회복 속도는 그 코어에 비례합니다. 데드아이는 켜 둔 동안 게이지를 쓰고,
     *        바닥나면 저절로 꺼집니다.
     */
    class SW_GF_API WesternSurvival
    {
    public:
        static constexpr uint32  kStateTag     = FourCcUtil::make( "WSRV" );
        static constexpr uint32  kStateVersion = 1;
        static constexpr float32 kCoreMax      = 100.0f;

        WesternSurvival();

        void initialize( const WesternCatalog* pCatalog );
        /**
         * @brief 시간을 흘립니다.
         * @param deltaTime 실제 초(게이지 · 데드아이) @param gameHours 이번에 흐른 게임 시간(코어) @param airTemperature 지금 기온(섭씨)
         */
        void update( float32 deltaTime, float32 gameHours, float32 airTemperature );
        /** @brief 날씨의 "temperature" 값을 지역 기본 기온에 더한 기온입니다(눈 덮인 산 · 사막). */
        static float32 computeAirTemperature( const WeatherSystem& weather, float32 regionBaseTemperature );

        /** @brief 음식을 먹습니다. 모르는 음식이면 false 입니다. */
        bool eat( const hashed_string& foodId );
        /** @brief 입은 옷을 통째로 바꿉니다(모르는 옷은 보온 0). */
        void    setClothing( const vector<hashed_string>& listClothing );
        float32 computeWarmth() const;
        float32 computeFeltTemperature( float32 airTemperature ) const { return airTemperature + computeWarmth(); }

        /** @brief 데드아이를 켭니다. 이미 켰거나 게이지가 최소량보다 적으면 false 입니다. */
        [[nodiscard]] bool activateDeadEye();
        void               deactivateDeadEye()
        {
            _bDeadEyeActive = SW_FALSE;
            _listMark.clear();
        }
        /** @brief 데드아이로 대상을 표시합니다. 꺼졌거나 단계의 표시 개수를 채웠으면 false 입니다. */
        [[nodiscard]] bool markTarget( uint64 targetId );
        void               setDeadEyeLevel( int32 level ) { _deadEyeLevel = level; }
        /** @brief 지금 시간 배율입니다(데드아이를 켰으면 단계의 배율, 아니면 1). */
        float32 getTimeScale() const;
        int32   getMarkLimit() const;
        bool    isDeadEyeActive() const { return _bDeadEyeActive != SW_FALSE; }

        float32               getCore( WesternCore core ) const { return _arrCore[static_cast<size_t>( core )]; }
        void                  setCore( WesternCore core, float32 value );
        float32               computeRegenScale( WesternCore core ) const;
        ResourceGauge&        getGauge( WesternCore core ) { return _arrGauge[static_cast<size_t>( core )]; }
        const ResourceGauge&  getGauge( WesternCore core ) const { return _arrGauge[static_cast<size_t>( core )]; }
        const vector<uint64>& getMarks() const { return _listMark; }
        /** @brief 입은 옷 · 표시한 대상 · 세 게이지 · 세 코어 · 데드아이 단계 · 켜짐을 씁니다. 카탈로그는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        static constexpr size_t kCoreCount = static_cast<size_t>( WesternCore::Count );

        vector<hashed_string> _listClothing;
        vector<uint64>        _listMark;
        ResourceGauge         _arrGauge[kCoreCount];
        float32               _arrCore[kCoreCount];
        const WesternCatalog* _pCatalog;
        int32                 _deadEyeLevel;
        uint8                 _bDeadEyeActive;
    };
} // namespace sw
