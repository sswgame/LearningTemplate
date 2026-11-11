/**
 * @file ParkLayout.h
 * @brief 공원 배치 데이터(`<ParkLayout>`)를 읽는 정의입니다. 정문, 입장료, 시작 자금, 평지 놀이기구, 코스터 배치를 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ThemePark.h"

namespace sw
{
    class CoasterLayoutCatalog;
    class XMLNode;
} // namespace sw

namespace sw
{
    /**
     * @brief `<ParkLayout>` 루트 속성의 스키마입니다(`XMLFields::readFields` — 속성 이름은 칸 이름에서 `_` 를 뗀 것).
     * @details 필수 칸(`Required`)이 빠지면 로드 오류이고, 표시가 없는 칸의 초기값이 기본값입니다. 읽는 코드에 리터럴 기본값이 없습니다.
     */
    REFLECT()
    struct ParkLayoutRootDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        float32 _gateX{}; ///< 정문 x(m)
        PROPERTY( Required )
        float32 _gateZ{}; ///< 정문 z(m)
        PROPERTY( Required, Min = 0 )
        int32 _startingCash{};
        PROPERTY( Min = 0 )
        int32 _entryFee{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief `<FlatRide>` 한 줄의 스키마입니다. */
    REFLECT()
    struct ParkFlatRideDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Required, Min = 0, Max = 10 )
        float32 _excitement{};
        PROPERTY( Required, Min = 0, Max = 10 )
        float32 _intensity{};
        PROPERTY( Required, Min = 0, Max = 10 )
        float32 _nausea{};
        PROPERTY( Required, Min = 5, Units = s )
        float32 _cycleTime{}; ///< 한 번 도는 시간(태우고 내리기 포함)
        PROPERTY( Required, Min = 1 )
        int32 _capacity{};
        PROPERTY( Required, Min = 0 )
        int32 _price{};
        PROPERTY( Required, Min = 0 )
        int32 _runningCost{}; ///< 분당 운영비
        PROPERTY( Required, Min = 0 )
        int32 _cost{}; ///< 짓는 값
        PROPERTY( Required )
        float32 _x{};
        PROPERTY( Required )
        float32 _z{};
        PROPERTY( Required )
        float32 _entranceX{}; ///< 줄 입구 x(m)
        PROPERTY( Required )
        float32 _entranceZ{}; ///< 줄 입구 z(m)
        PROPERTY( Required )
        string _shape{}; ///< 내장 도형 이름
        PROPERTY( Required, Min = 0.01 )
        float32 _sizeX{};
        PROPERTY( Required, Min = 0.01 )
        float32 _sizeY{};
        PROPERTY( Required, Min = 0.01 )
        float32 _sizeZ{};
        PROPERTY()
        float32 _spin{ 0.0f }; ///< 운행 중 도는 빠르기(rad/s) — 적지 않으면 돌지 않는다
        PROPERTY( Required, Meta = "Color" )
        float4 _color{}; ///< `r,g,b,a` 네 성분을 모두 적는다
    };
} // namespace sw

namespace sw
{
    /** @brief `<Coaster>` 한 줄의 스키마입니다. 레이아웃은 코스터 카탈로그의 id 입니다. */
    REFLECT()
    struct ParkCoasterDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _layout{};
        PROPERTY( Required )
        float32 _x{};
        PROPERTY( Required )
        float32 _z{};
        PROPERTY()
        float32 _heading{ 0.0f }; ///< 스테이션 방향(도)
        PROPERTY( Required, Min = 1 )
        int32 _capacity{};
        PROPERTY( Required, Min = 0, Units = s )
        float32 _loadTime{};
        PROPERTY( Required, Min = 0 )
        int32 _cost{};
        PROPERTY( Required, Meta = "Color" )
        float4 _color{}; ///< `r,g,b,a` 네 성분을 모두 적는다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ParkRidePlacement
     * @brief 지을 수 있는 놀이기구 하나(배치 데이터의 한 줄)입니다. `_layoutID` 가 비어 있으면 평지 놀이기구이고, 아니면 코스터 레이아웃입니다.
     */
    struct ParkRidePlacement
    {
        ParkRide      _ride{};
        hashed_string _layoutID{}; ///< 비어 있으면 평지 놀이기구이다
        string        _shape{ "Cylinder" };
        float3        _position{};
        float3        _size{ 4.0f, 1.0f, 4.0f };
        float4        _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float32       _heading{ 0.0f }; ///< 코스터 스테이션의 방향(도)
        float32       _spin{ 0.0f };    ///< 평지 놀이기구가 운행 중 도는 빠르기(rad/s)
        float32       _loadTime{ 15.0f };
        int32         _buildCost{ 1000 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ParkLayout
     * @brief 공원 배치 데이터 한 장입니다. 게임은 읽은 정의로 놀이기구를 만들기만 하며, 형식과 검증은 이 클래스가 한 곳에서 맡습니다.
     * @details 코스터 줄(`<Coaster layout="…">`)은 코스터 레이아웃 카탈로그에서 찾는다. 없는 레이아웃은 오류이고 건너뛴다.
     *          파일에서 읽는 길은 공유 캐시 하나다(`SharedAssetCache<ParkLayout>::acquire`) — 로더가 같은 팩의 코스터 카탈로그를 공유 캐시에서 받아 쥐고(`getCoasterLayouts`),
     *          쓰는 쪽은 코스터 경로를 따로 적지 않는다(기준 하나 — 같은 팩의 종류 파일 `GamePackDataUtil::tryMakeSiblingKindPath`). 테스트 · 에디터 미리 보기는 `loadFromXMLText` 에 카탈로그를 넘긴다.
     *          속성의 형식 · 필수 · 범위는 스키마 셋(`ParkLayoutRootDef` · `ParkFlatRideDef` · `ParkCoasterDef`)이 정하고 하나라도 어기면 로드가 실패한다.
     */
    class SW_KIT_API ParkLayout
    {
        friend struct AssetTypeLoader;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_ASSET_TYPE` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "parklayout";

        ParkLayout();

        /** @brief XML 문자열에서 읽습니다(테스트와 도구용). 코스터 카탈로그는 쥐지 않습니다(`getCoasterLayouts` 가 빈다). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, const CoasterLayoutCatalog& layouts, string_view sourceName = {} );

        const float3&                    getGatePosition() const { return _gatePosition; }
        int32                            getEntryFee() const { return _entryFee; }
        int32                            getStartingCash() const { return _startingCash; }
        const vector<ParkRidePlacement>& getPlacements() const { return _listPlacement; }
        /** @brief 파일에서 읽을 때 받은 같은 팩의 코스터 레이아웃입니다(공유 캐시의 한 벌). 글에서 읽었으면 nullptr 입니다. */
        const shared_ptr<const CoasterLayoutCatalog>& getCoasterLayouts() const { return _pCoasterLayouts; }

    private:
        /**
         * @brief 리소스 경로의 `<ParkLayout>` 을 읽습니다. 같은 팩의 코스터 카탈로그를 공유 캐시에서 먼저 받습니다. 놀이기구가 하나도 없으면 false 입니다.
         * @details 종류의 로더(공유 캐시)만 부른다. 코스터 파일 이름을 다시 적지 않는다 — 같은 팩의 코스터 종류 파일이다(`GamePackDataUtil::tryMakeSiblingKindPath`).
         */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        [[nodiscard]] bool loadRoot( const XMLNode& root, const CoasterLayoutCatalog& layouts, string_view sourceName );
        [[nodiscard]] bool loadFlatRide( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadCoaster( const XMLNode& node, const CoasterLayoutCatalog& layouts, string_view sourceName );

    private:
        vector<ParkRidePlacement>              _listPlacement;
        float3                                 _gatePosition;
        int32                                  _entryFee;
        int32                                  _startingCash;
        shared_ptr<const CoasterLayoutCatalog> _pCoasterLayouts; ///< 파일에서 읽을 때 받은 코스터 레이아웃(공유 캐시의 한 벌)
    };

    /**
     * @brief 공원 배치를 경로마다 한 벌 나눠 줍니다("parklayout", 인스턴스화는 `ThemeParkDataKind.cpp`). 제자리에서 다시 읽습니다.
     * @details 게임 모드는 시작할 때 배치를 자기 놀이기구 목록으로 옮겨 짓는다(배치 데이터 → 실행 상태).
     */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, ParkLayout );
} // namespace sw
