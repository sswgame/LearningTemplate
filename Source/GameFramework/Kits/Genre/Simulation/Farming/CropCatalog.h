/**
 * @file CropCatalog.h
 * @brief 작물 정의를 담습니다. 씨앗, 수확물, 자라는 날 수, 다시 자람, 계절, 가격을 가지며 `crops.xml` 에서 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

#include <algorithm>

namespace sw
{
    class ItemCatalog;
    class XMLNode;

    /**
     * @brief 작물 한 종입니다.
     * @details 물을 준 날에만 하루씩 자란다. `_growthDays` 번 자라면 거둘 수 있고, `_regrowDays` 가 0 이 아니면 거둔 뒤에도 남아 그만큼 다시 자란다
     *          (토마토, 옥수수). 자라는 계절(`_listSeason`, 시계의 계절 이름) 밖으로 계절이 바뀌면 시든다.
     */
    REFLECT()
    struct CropDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Name = "seed", Required )
        hashed_string _seedItem{}; ///< 심을 때 쓰는 아이템
        PROPERTY( Name = "produce", Required )
        hashed_string _produceItem{}; ///< 거두면 받는 아이템
        PROPERTY( Name = "days", Min = 1 )
        int32 _growthDays{ 4 };
        PROPERTY( Name = "regrow", Min = 0 )
        int32 _regrowDays{ 0 }; ///< 0 이면 한 번 거두고 끝난다
        PROPERTY( Min = 0 )
        int32 _seedPrice{ 20 };
        PROPERTY( Min = 0 )
        int32 _sellPrice{ 60 };
        PROPERTY( Name = "harvest", Min = 1 )
        int32 _harvestCount{ 1 }; ///< 한 번에 받는 수
        PROPERTY( Meta = "Color" )
        float4 _color{ 0.3f, 0.7f, 0.3f, 1.0f }; ///< 다 자란 작물의 색(`color="r,g,b,a"`). 적지 않으면 초록이다

        vector<hashed_string> _listSeason{}; ///< 자라는 계절이다(`seasons`, 필수). 시계의 계절 이름이며 `WorldClockSettings::_listSeason` 과 같다

        /** @brief @p season 에 자라면 true 를 반환합니다. */
        bool growsIn( const hashed_string& season ) const { return VectorUtil::contains( _listSeason, season ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @class CropCatalog
     * @brief `<CropCatalog><Crop id="turnip" name="Turnip" seed="turnip_seed" produce="turnip" days="4" regrow="0" seasons="Spring,Fall"
     *        seedPrice="20" sellPrice="60" harvest="1" color="0.92,0.88,0.95,1"/></CropCatalog>` 를 읽습니다. 씨앗이나 수확물 아이템으로도 찾을 수 있습니다.
     */
    class SW_KIT_API CropCatalog : public XMLCatalog<CropCatalog>
    {
        friend class XMLCatalog<CropCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "crops";

        CropCatalog();

        /** @brief 작물을 추가합니다(같은 id 는 바꿉니다). */
        void addCrop( const CropDef& crop );

        const CropDef* findCrop( const hashed_string& cropID ) const { return _catalog.find( cropID ); }
        /** @brief 씨앗 아이템으로 작물을 찾습니다(해시 조회 한 번). */
        const CropDef* findCropBySeed( const hashed_string& seedItem ) const;
        /**
         * @brief 아이템 하나의 판매가를 반환합니다. 수확물은 `_sellPrice`, 씨앗은 `_seedPrice` 의 절반이며, 모르는 아이템은 0 입니다.
         */
        int32                  findSellPrice( const hashed_string& itemID ) const;
        const vector<CropDef>& getCrops() const { return _catalog.getAll(); }

        /** @brief 읽기 전에 알려 둘 계절 이름을 정합니다. 모르는 계절이 적힌 작물은 오류로 뺍니다(비우면 검사하지 않습니다). */
        void setKnownSeasons( const vector<hashed_string>& listSeason ) { _listKnownSeason = listSeason; }
        /** @brief 작물마다 씨앗과 수확물 아이템을 @p inoutItems 에 더합니다(이미 있으면 그대로 둡니다). 플레이어 가방(`Inventory`)이 겹침 수를 물을 때 씁니다. */
        void fillItemCatalog( ItemCatalog& inoutItems, int32 maxStack ) const;

    private:
        static constexpr const utf8* kXMLRootName = "CropCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Crop>` 하나를 스키마로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadCrop( const XMLNode& node, string_view sourceName );
        /** @brief 씨앗과 수확물에서 작물 index 로 가는 맵을 다시 만듭니다(작물이 추가될 때 호출합니다). */
        void rebuildItemIndex();

        GameCatalog<CropDef>                 _catalog;         ///< 읽은 순서이며 가게 진열 순서이다
        unordered_map<hashed_string, uint32> _mapSeedIndex;    ///< 씨앗 아이템에서 index 로 가는 맵
        unordered_map<hashed_string, uint32> _mapProduceIndex; ///< 수확물 아이템에서 index 로 가는 맵
        vector<hashed_string>                _listKnownSeason; ///< 읽을 때 검사하는 계절 이름이다(비어 있으면 검사하지 않는다)
    };
} // namespace sw
