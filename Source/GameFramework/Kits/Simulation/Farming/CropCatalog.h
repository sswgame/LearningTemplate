/**
 * @file CropCatalog.h
 * @brief 작물 정의 — 씨앗 · 수확물 · 자라는 날 수 · 다시 자람 · 계절 · 값입니다(crops.xml).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Data/GameCatalog.h"
#include "GameFramework/Base/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Simulation/Farming/FarmCalendar.h"

namespace sw
{
    class XmlNode;

    /**
     * @brief 작물 한 종입니다.
     * @details 물 준 날만 하루 자랍니다. `_growthDays` 번 자라면 거둘 수 있고, `_regrowDays` 가 0 이 아니면 거둔 뒤에도 남아 그만큼 다시 자랍니다
     *          (토마토 · 옥수수). 자라는 계절(`_seasonMask`) 밖으로 계절이 바뀌면 시듭니다.
     */
    struct CropDef
    {
        hashed_string _id{};
        string        _name{};
        hashed_string _seedItem{};    ///< 심을 때 쓰는 아이템
        hashed_string _produceItem{}; ///< 거두면 받는 아이템
        int32         _growthDays{ 4 };
        int32         _regrowDays{ 0 }; ///< 0 이면 한 번 거두고 끝
        int32         _seedPrice{ 20 };
        int32         _sellPrice{ 60 };
        int32         _harvestCount{ 1 }; ///< 한 번에 받는 수
        uint8         _seasonMask{ 0 };   ///< `makeFarmSeasonBit` 의 합

        /** @brief @p season 에 자라면 true 입니다. */
        bool growsIn( FarmSeason season ) const { return ( _seasonMask & makeFarmSeasonBit( season ) ) != 0; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class CropCatalog
     * @brief `<CropCatalog><Crop id="turnip" name="Turnip" seed="turnip_seed" produce="turnip" days="4" regrow="0" seasons="Spring,Fall"
     *        seedPrice="20" sellPrice="60" harvest="1"/></CropCatalog>` 를 읽습니다. 씨앗 · 수확물 아이템으로도 찾습니다.
     */
    class SW_GF_API CropCatalog : public XmlCatalog<CropCatalog>
    {
        friend class XmlCatalog<CropCatalog>;

    public:
        CropCatalog();

        /** @brief 작물을 더합니다(같은 id 는 바꾼다). */
        void addCrop( const CropDef& crop );

        const CropDef* findCrop( const hashed_string& cropId ) const { return _catalog.find( cropId ); }
        /** @brief 씨앗 아이템으로 작물을 찾습니다(해시 한 번). */
        const CropDef* findCropBySeed( const hashed_string& seedItem ) const;
        /**
         * @brief 아이템 하나의 판매가입니다 — 수확물은 `_sellPrice`, 씨앗은 `_seedPrice` 의 절반. 모르는 아이템은 0 입니다.
         */
        int32                  findSellPrice( const hashed_string& itemId ) const;
        const vector<CropDef>& getCrops() const { return _catalog.getAll(); }

    private:
        static constexpr const utf8* kXmlRootName = "CropCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );
        /** @brief 씨앗 · 수확물 → 작물 자리 맵을 다시 짓습니다(작물이 더해질 때). */
        void rebuildItemIndex();

        GameCatalog<CropDef>                 _catalog;         ///< 읽은 순서(가게 진열 순서)
        unordered_map<hashed_string, uint32> _mapSeedIndex;    ///< 씨앗 아이템 → 자리
        unordered_map<hashed_string, uint32> _mapProduceIndex; ///< 수확물 아이템 → 자리
    };
} // namespace sw
