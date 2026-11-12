/**
 * @file PlayerStats.h
 * @brief 로컬 통계입니다. 통계 정의는 데이터(카운터, 최대, 최소, 시간)로 두고, 값은 프로필마다 저장하며, 바뀌면 알립니다. 업적의 바탕이 되며 Steam Stats 에 해당합니다.
 * @details 업적 정의도 같은 파일에 둡니다(`<Achievement id stat threshold>`) — 스팀 앱 설정의 "Stats & Achievements" 가 한 표인 것과 같다. 푸는 일은 `AchievementTracker` 가 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 통계가 값을 받는 방법입니다. 데이터(`kind`)에는 이름 그대로 적습니다. */
    ENUM()
    enum class StatKind : uint8
    {
        Counter = 0, ///< 더하기만 한다(처치 수, 걸음 수). 줄지 않는다
        Max,         ///< 가장 큰 값을 남긴다(최고 점수, 최대 콤보)
        Min,         ///< 가장 작은 값을 남긴다(최단 기록). 아직 없으면 첫 값을 그대로 쓴다
        Time         ///< 초를 더한다(플레이 시간, 그 무기를 든 시간)
    };

    /** @brief 통계 하나의 정의입니다. */
    REFLECT()
    struct SW_GF_API StatDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        string _name{}; ///< 표시 이름(없으면 id)
        PROPERTY( Name = "max", Min = 0.0 )
        float64 _maxValue{ 0.0 }; ///< 0 보다 크면 값의 상한이다. 카운터가 넘치지 않게 하며, Steam 의 maxchange 에 해당한다
        PROPERTY()
        StatKind _kind{ StatKind::Counter };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 업적 하나의 정의입니다. 통계 하나가 문턱에 닿으면 풀립니다(스팀의 "진행 통계" 업적과 같은 모양).
     * @details 최소 통계(최단 기록)는 값이 문턱 이하가 되면, 그 밖은 문턱 이상이 되면 풀린다. 통계는 같은 카탈로그에 있어야 한다(`AchievementTracker::initialize` 가 본다).
     */
    REFLECT()
    struct SW_GF_API AchievementDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        string _name{}; ///< 표시 이름(없으면 id)
        PROPERTY( Required )
        hashed_string _statID{}; ///< 문턱을 보는 통계 id
        PROPERTY( Min = 0.0 )
        float64 _threshold{ 0.0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 값이 바뀐 변화 하나입니다. `PlayerStats` 가 알립니다. */
    struct StatChange
    {
        hashed_string _id{};
        float64       _oldValue{ 0.0 };
        float64       _newValue{ 0.0 };
        StatKind      _kind{ StatKind::Counter };
    };
} // namespace sw

namespace sw
{
    /**
     * @class StatCatalog
     * @brief `<StatCatalog><Stat id="enemies_killed" kind="Counter" name="Enemies" max="1000000"/><Achievement id="first_blood" stat="enemies_killed" threshold="1"/>…</StatCatalog>` 를 읽습니다.
     * @details 알 수 없는 `kind` 는 오류이고 그 정의는 뺍니다. 같은 id 의 업적은 뒤의 것이 바꿉니다.
     */
    class SW_GF_API StatCatalog : public XMLCatalog<StatCatalog>
    {
        friend class XMLCatalog<StatCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<StatCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".stats.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `PlayerStats.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "StatCatalog", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        StatCatalog();

        /** @brief 코드로 정의를 추가합니다. 같은 id 가 있으면 교체합니다. */
        void addStat( const StatDef& def );

        const StatDef*         findStat( const hashed_string& id ) const;
        const vector<StatDef>& getStats() const { return _listStat; }

        const AchievementDef*         findAchievement( const hashed_string& id ) const;
        const vector<AchievementDef>& getAchievements() const { return _listAchievement; }

    private:
        static constexpr const utf8* kXMLRootName = "StatCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadStat( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadAchievement( const XMLNode& node, string_view sourceName );

    private:
        vector<StatDef>                      _listStat;
        unordered_map<hashed_string, uint32> _mapIndex;
        vector<AchievementDef>               _listAchievement;
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<StatCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, StatCatalog );
} // namespace sw

namespace sw
{
    /**
     * @class PlayerStats
     * @brief 프로필 하나의 통계 값입니다. 정의는 카탈로그가 주며 소유하지 않고 참조만 합니다. 값은 id 로 보관합니다.
     * @details - 값은 `increment`(카운터), `submit`(최대와 최소로, 기록이 좋아질 때만 바뀜), `addTime`(시간)으로 바꾼다. 정의와 다른 종류로 부르면 경고를 남기고 무시한다.
     *            Steam 이 INT 통계에 FLOAT 를 넣으면 거절하는 것과 같다. 알 수 없는 id 도 경고를 남기고 무시한다.
     *          - 값이 실제로 바뀌면 등록한 듣는 쪽마다 `StatChange` 를 넘긴다. 업적, UI, 텔레메트리가 여기에 연결된다.
     *          - 저장은 프로필과 함께 한다. `writeState` 와 `readState` 는 id 이름으로 쓰므로 정의 순서가 바뀌어도 맞고, 없어진 정의의 값은 경고를 남기고 버린다.
     *            `saveToFile` 과 `loadFromFile` 은 원자적으로 쓴다. 읽기는 알림을 내지 않는다.
     *          Steam 의 `ISteamUserStats`(INT, FLOAT, 최대 변화량), 언리얼 Online Stats, 유니티 게임 서비스 통계의 로컬 버전이다. 서버 동기화와 평균 비율은 없다.
     */
    class SW_GF_API PlayerStats
    {
    public:
        using ChangeDelegate = Delegate<void( const StatChange& change )>;

        PlayerStats();

        /** @brief 카탈로그를 설정합니다. 소유하지 않고 참조만 하므로 이 객체보다 오래 살아 있어야 합니다. 값은 비웁니다. */
        void initialize( const StatCatalog* pCatalog );

        /** @brief 카운터에 @p amount 를 더합니다. 0 이상이어야 하며, 값이 바뀌었으면 true 입니다. */
        bool increment( const hashed_string& id, int64 amount = 1 );
        /** @brief 최대나 최소 통계에 값을 제출합니다. 기록이 좋아질 때만 바뀌며, 바뀌었으면 true 입니다. */
        [[nodiscard]] bool submit( const hashed_string& id, float64 value );
        /** @brief 시간 통계에 @p seconds 를 더합니다. 0 이상이어야 하며, 값이 바뀌었으면 true 입니다. */
        bool addTime( const hashed_string& id, float64 seconds );

        /** @brief 통계 값입니다. 아직 없으면 0 입니다. 최소 통계의 첫 값 전이거나 알 수 없는 id 가 해당합니다. */
        float64 getValue( const hashed_string& id ) const;
        int64   getCount( const hashed_string& id ) const { return static_cast<int64>( getValue( id ) ); }
        /** @brief 값이 한 번이라도 들어왔으면 true 입니다. 최소 통계에서는 0 과 "없음" 이 다르기 때문에 필요합니다. */
        bool hasValue( const hashed_string& id ) const;
        /** @brief 모든 값을 지웁니다. 알림은 내지 않으며, 프로필을 새로 만들 때 씁니다. */
        void reset();

        /** @brief 값이 바뀔 때 호출할 델리게이트를 등록하고 그 구독을 돌려줍니다. 구독이 사라지면 뗍니다. */
        [[nodiscard]] Subscription registerChangeListener( const ChangeDelegate& listener );

        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다. 카탈로그에 없는 id 의 값은 경고를 남기고 버립니다. */
        SW_DECLARE_STATE_IO();
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        [[nodiscard]] bool loadFromFile( string_view path );

    private:
        /** @brief 정의를 찾고 종류를 확인합니다. 맞지 않으면 경고를 남기고 nullptr 를 반환합니다. */
        const StatDef* findDefForUse( const hashed_string& id, StatKind expectedKind, StatKind alternateKind ) const;
        /** @brief 값을 바꾸고(상한 적용) 바뀌었으면 알립니다. */
        [[nodiscard]] bool applyValue( const StatDef& def, float64 newValue );

    private:
        const StatCatalog*                           _pCatalog;
        unordered_map<hashed_string, float64>        _mapValue;
        MulticastDelegate<void( const StatChange& )> _changedMulticast; ///< 값이 바뀐 것을 듣는 쪽(구독이 소유)
    };
} // namespace sw
