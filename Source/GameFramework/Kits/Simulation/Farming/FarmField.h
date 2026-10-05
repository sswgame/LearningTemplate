/**
 * @file FarmField.h
 * @brief 밭 — 칸마다 갈기 · 물 · 작물 · 자람을 들고, 하루가 넘어갈 때 자람 · 시듦 · 비를 처리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Simulation/Farming/FarmCalendar.h"
#include "GameFramework/Utility/GridTopology.h"

namespace sw
{
    struct CropDef;

    class Archive;
    class CropCatalog;

    /** @brief 밭 한 칸에 무엇을 하려다 어떻게 됐는지입니다. UI 가 이유를 띄우고 시험이 읽습니다. */
    enum class FarmActionResult : uint8
    {
        Done = 0,       ///< 했다
        OutOfBounds,    ///< 밭 밖
        NotTilled,      ///< 갈지 않은 땅(물 · 심기)
        AlreadyTilled,  ///< 이미 갈았다(갈기)
        AlreadyWatered, ///< 오늘 이미 물을 줬다
        Occupied,       ///< 작물이 있다(갈기 · 심기)
        NoCrop,         ///< 작물이 없다(거두기)
        NotReady,       ///< 아직 덜 자랐다(거두기)
        UnknownSeed,    ///< 카탈로그에 없는 씨앗
        OutOfSeason     ///< 지금 계절에 자라지 않는 씨앗
    };

    /** @brief 결과 이름입니다(로그 · UI). */
    SW_GF_API const utf8* toString( FarmActionResult result );

    /** @brief 밭 한 칸입니다. */
    struct FarmTile
    {
        hashed_string _cropId{};    ///< 비었으면 작물 없음
        int32         _growth{ 0 }; ///< 물 받고 지난 날 수
        uint8         _bTilled{ SW_FALSE };
        uint8         _bWatered{ SW_FALSE };  ///< 오늘 물을 받았다(비 포함)
        uint8         _bReady{ SW_FALSE };    ///< 거둘 수 있다
        uint8         _bWithered{ SW_FALSE }; ///< 계절이 지나 시들었다(거두면 치워진다)

        bool hasCrop() const { return _cropId.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class FarmField
     * @brief 가로 × 세로 칸의 밭입니다. 행동은 칸 좌표로 하고 결과는 `FarmActionResult` 입니다.
     * @details 하루 넘김(`advanceDay`)의 규칙 — 하베스트 문과 같다:
     *          1. 어제 물 받은 작물만 하루 자란다. 자란 날이 `_growthDays` 에 닿으면 거둘 수 있다.
     *          2. 새 계절에 자라지 않는 작물은 시든다(거두면 아무것도 없이 치워진다).
     *          3. 물은 매일 마른다. 비 오는 날은 갈아 둔 칸이 모두 물을 받은 채로 시작한다.
     *          카탈로그는 빌려 씁니다 — 밭보다 오래 살아야 합니다.
     */
    class SW_GF_API FarmField
    {
    public:
        FarmField();

        /** @brief 크기를 정하고 모든 칸을 갈지 않은 빈 땅으로 둡니다. */
        void initialize( int32 width, int32 height, const CropCatalog* pCatalog );

        FarmActionResult till( int32 x, int32 y );
        FarmActionResult water( int32 x, int32 y );
        FarmActionResult plant( int32 x, int32 y, const hashed_string& seedItem, FarmSeason season );
        /**
         * @brief 거둡니다. 다 자랐으면 @p outProduceItem · @p outCount 에 받은 것을 적습니다. 시든 작물은 치우고 `Done` 이지만 받는 것은 없습니다(개수 0).
         * @details 다시 자라는 작물은 남고 `_regrowDays` 만큼 다시 자라야 합니다.
         */
        FarmActionResult harvest( int32 x, int32 y, hashed_string& outProduceItem, int32& outCount );
        /** @brief 하루를 넘깁니다 — 자람 → 시듦(새 계절 @p newSeason) → 물 마름 · 비(@p bRain). */
        void advanceDay( FarmSeason newSeason, bool bRain );

        /** @brief 칸입니다. 밖이면 nullptr 입니다. */
        const FarmTile* findTile( int32 x, int32 y ) const;
        int32           getWidth() const { return _topology._width; }
        int32           getHeight() const { return _topology._height; }
        /** @brief 작물이 있는 칸 수입니다(시든 것 포함). */
        uint32 getCropCount() const;
        /** @brief 거둘 수 있는 칸 수입니다. */
        uint32 getReadyCount() const;
        /** @brief 칸의 작물이 다 자라기까지의 비율(0..1)입니다. 작물이 없으면 0 입니다. */
        float32 computeGrowthRatio( int32 x, int32 y ) const;

        /** @brief 칸마다 작물 · 자람 · 상태를 씁니다(핫 리로드 · 세이브). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 칸을 바꿉니다. 밭 크기가 다르거나 깨졌으면 false 이고 그대로입니다(카탈로그는 `initialize` 의 것). */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        FarmTile*      findTileMutable( int32 x, int32 y );
        const CropDef* findCropDef( const FarmTile& tile ) const;

        vector<FarmTile>   _listTile; ///< 칸마다(`_topology` 의 칸 번호)
        const CropCatalog* _pCatalog;
        GridTopology       _topology;
    };
} // namespace sw
