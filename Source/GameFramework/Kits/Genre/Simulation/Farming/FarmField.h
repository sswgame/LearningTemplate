/**
 * @file FarmField.h
 * @brief 밭을 구현합니다. 타일마다 갈기, 물, 작물, 자람을 가지고, 하루가 넘어갈 때 자람, 시듦, 비를 처리합니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/Base/World/Land/LandRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CropDef;

    class Archive;
    class CropCatalog;

    /** @brief 밭 타일 하나에 무언가를 하려다 어떻게 됐는지 나타냅니다. UI 가 이유를 띄우고 테스트가 읽습니다. */
    enum class FarmActionResult : uint8
    {
        Done = 0,       ///< 했다
        OutOfBounds,    ///< 밭 밖이다
        NotTilled,      ///< 갈지 않은 땅이다(물 주기와 심기)
        AlreadyTilled,  ///< 이미 갈았다(갈기)
        AlreadyWatered, ///< 오늘 이미 물을 줬다
        Occupied,       ///< 작물이 있다(갈기와 심기)
        NoCrop,         ///< 작물이 없다(거두기)
        NotReady,       ///< 아직 덜 자랐다(거두기)
        UnknownSeed,    ///< 카탈로그에 없는 씨앗이다
        OutOfSeason     ///< 지금 계절에 자라지 않는 씨앗이다
    };

    /** @brief 결과 이름을 반환합니다(로그와 UI 용). */
    SW_GF_API const utf8* toString( FarmActionResult result );

    /** @brief 밭 타일 하나입니다. */
    struct FarmTile
    {
        hashed_string _cropID{};    ///< 비어 있으면 작물이 없다
        int32         _growth{ 0 }; ///< 물을 받고 지난 날 수
        uint8         _bTilled{ SW_FALSE };
        uint8         _bWatered{ SW_FALSE };  ///< 오늘 물을 받았다(비 포함)
        uint8         _bReady{ SW_FALSE };    ///< 거둘 수 있다
        uint8         _bWithered{ SW_FALSE }; ///< 계절이 지나 시들었다(거두면 치워진다)

        bool hasCrop() const { return _cropID.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class FarmField
     * @brief 가로 x 세로 타일의 밭입니다. 행동은 타일 좌표로 하고 결과는 `FarmActionResult` 입니다.
     * @details 하루 넘김(`advanceDay`)의 규칙은 하베스트 문과 같다.
     *          1. 어제 물을 받은 작물만 하루 자란다. 자란 날이 `_growthDays` 에 닿으면 거둘 수 있다.
     *          2. 새 계절에 자라지 않는 작물은 시든다(거두면 아무것도 없이 치워진다).
     *          3. 물은 매일 마른다. 비 오는 날은 갈아 둔 타일이 모두 물을 받은 채로 시작한다.
     *          카탈로그는 빌려 쓰므로 밭보다 오래 살아야 한다.
     */
    class SW_GF_API FarmField
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "FFLD" );
        static constexpr uint32 kStateVersion = 1;

        FarmField();

        /** @brief 크기를 정하고 모든 타일을 갈지 않은 빈 땅으로 둡니다. */
        void initialize( int32 width, int32 height, const CropCatalog* pCatalog );
        /**
         * @brief 공유 땅을 빌립니다. 밭은 위치가 정해진 땅이라 밭 전체(막힘 아님)를 한 번에 얻으며, 밭 타일 (0, 0) 이 땅 셀 @p origin 에 해당합니다.
         * @return 땅의 그 위치에 남의 셀이 있으면 false 이며 땅을 묶지 않는다(밭은 단독으로 동작한다). @p pLand 가 nullptr 이면 빌린 땅을 반납하고 true 이다. `initialize` 뒤에 호출해야 한다.
         */
        [[nodiscard]] bool bindLand( LandRegistry* pLand, const int2& origin );

        FarmActionResult till( int32 x, int32 y );
        FarmActionResult water( int32 x, int32 y );
        FarmActionResult plant( int32 x, int32 y, const hashed_string& seedItem, const hashed_string& season );
        /**
         * @brief 거둡니다. 다 자랐으면 @p outProduceItem 과 @p outCount 에 받은 것을 적습니다. 시든 작물은 치우고 `Done` 을 반환하지만 받는 것은 없습니다(개수 0).
         * @details 다시 자라는 작물은 남고, `_regrowDays` 만큼 다시 자라야 한다.
         */
        FarmActionResult harvest( int32 x, int32 y, hashed_string& outProduceItem, int32& outCount );
        /** @brief 하루를 넘깁니다. 자람, 시듦(새 계절 @p newSeason 은 시계의 계절 이름), 물 마름과 비(@p bRain) 순서로 처리합니다. */
        void advanceDay( const hashed_string& newSeason, bool bRain );

        /** @brief 타일을 반환합니다. 밖이면 nullptr 입니다. */
        const FarmTile* findTile( int32 x, int32 y ) const;
        int32           getWidth() const { return _topology._width; }
        int32           getHeight() const { return _topology._height; }
        /** @brief 작물이 있는 타일 수를 반환합니다(시든 것 포함). */
        uint32 getCropCount() const;
        /** @brief 거둘 수 있는 타일 수를 반환합니다. */
        uint32 getReadyCount() const;
        /** @brief 타일의 작물이 다 자라기까지의 비율(0..1)을 반환합니다. 작물이 없으면 0 입니다. */
        float32 computeGrowthRatio( int32 x, int32 y ) const;

        /** @brief 타일마다 작물, 자람, 상태를 씁니다(핫 리로드와 세이브용). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 타일을 복원합니다. 밭 크기가 다르거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다(카탈로그는 `initialize` 에서 받은 것을 씁니다). */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        FarmTile*      findTileMutable( int32 x, int32 y );
        const CropDef* findCropDef( const FarmTile& tile ) const;

        vector<FarmTile>   _listTile; ///< 타일마다의 상태이다(`_topology` 의 타일 번호)
        const CropCatalog* _pCatalog;
        GridTopology       _topology;
        LandBinding        _land; ///< 빌린 공유 땅(없으면 단독 동작)
    };
} // namespace sw
