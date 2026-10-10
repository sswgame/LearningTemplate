/**
 * @file PlayerStats.h
 * @brief 로컬 통계 — 통계 정의는 데이터(카운터 · 최대 · 최소 · 시간), 값은 프로필마다, 바뀌면 알림(업적의 바탕 — Steam Stats 의 자리)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 통계가 값을 받는 방법입니다. 데이터(`kind`)는 이름 그대로 적습니다. */
    enum class StatKind : uint8
    {
        Counter = 0, ///< 더하기만 한다(처치 수 · 걸음) — 줄지 않는다
        Max,         ///< 가장 큰 값을 남긴다(최고 점수 · 최대 콤보)
        Min,         ///< 가장 작은 값을 남긴다(최단 기록) — 아직 없으면 첫 값
        Time         ///< 초를 더한다(플레이 시간 · 그 무기를 든 시간)
    };

    [[nodiscard]] SW_GF_API bool parseStatKind( string_view text, StatKind& outKind );
    SW_GF_API const utf8*        toString( StatKind kind );

    /** @brief 통계 하나의 정의입니다. */
    struct StatDef
    {
        hashed_string _id{};
        string        _name{};          ///< 보이는 이름(없으면 id)
        float64       _maxValue{ 0.0 }; ///< 0 보다 크면 값의 상한(카운터가 넘치지 않게 — Steam 의 maxchange 자리)
        StatKind      _kind{ StatKind::Counter };
    };
} // namespace sw

namespace sw
{
    /** @brief 값이 바뀐 일 하나입니다(`PlayerStats` 의 알림). */
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
     * @brief `<Stats><Stat id="enemies_killed" kind="Counter" name="Enemies" max="1000000"/>…</Stats>` 를 읽습니다. 모르는 `kind` 는 오류이고 그 정의는 뺍니다.
     */
    class SW_GF_API StatCatalog : public XMLCatalog<StatCatalog>
    {
        friend class XMLCatalog<StatCatalog>;

    public:
        StatCatalog();

        /** @brief 코드로 정의를 더합니다(같은 id 는 바꾼다). */
        void addStat( const StatDef& def );

        const StatDef*         findStat( const hashed_string& id ) const;
        const vector<StatDef>& getStats() const { return _listStat; }

    private:
        static constexpr const utf8* kXMLRootName = "Stats"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        vector<StatDef>                      _listStat;
        unordered_map<hashed_string, uint32> _mapIndex;
    };
} // namespace sw

namespace sw
{
    /**
     * @class PlayerStats
     * @brief 프로필 하나의 통계 값입니다. 정의는 카탈로그가 주고(빌려 쓴다), 값은 id 로 듭니다.
     * @details - `increment`(카운터) · `submit`(최대 · 최소 — 기록이 좋아질 때만 바뀐다) · `addTime`(시간). 정의와 다른 종류로 부르면 경고하고 무시한다
     *            (Steam 이 INT 통계에 FLOAT 를 넣으면 거절하는 것과 같다). 모르는 id 는 경고하고 무시한다.
     *          - 값이 실제로 바뀌면 등록한 듣는 쪽마다 `StatChange` 를 넘긴다 — 업적 · UI · 텔레메트리가 여기에 붙는다.
     *          - 저장은 프로필과 함께: `writeState` · `readState`(id 이름으로 적는다 — 정의 순서가 바뀌어도 맞고, 없어진 정의의 값은 알리고 버린다),
     *            `saveToFile` · `loadFromFile`(원자적 쓰기). 읽기는 알림을 내지 않는다.
     *          Steam `ISteamUserStats`(INT · FLOAT · 최대 변화량) · 언리얼 Online Stats · 유니티 게임 서비스 통계의 로컬 판입니다(서버 동기화 · 평균 비율 없음).
     */
    class SW_GF_API PlayerStats
    {
    public:
        using ChangeDelegate = Delegate<void( const StatChange& change )>;

        PlayerStats();

        /** @brief 카탈로그를 둡니다(빌린다 — 이 객체보다 오래 산다). 값은 비웁니다. */
        void initialize( const StatCatalog* pCatalog );

        /** @brief 카운터에 @p amount(0 이상)를 더합니다. 바뀌었으면 true 입니다. */
        bool increment( const hashed_string& id, int64 amount = 1 );
        /** @brief 최대 · 최소 통계에 값을 냅니다 — 기록이 좋아질 때만 바뀐다. 바뀌었으면 true 입니다. */
        bool submit( const hashed_string& id, float64 value );
        /** @brief 시간 통계에 @p seconds(0 이상)를 더합니다. 바뀌었으면 true 입니다. */
        bool addTime( const hashed_string& id, float64 seconds );

        /** @brief 값입니다. 아직 없으면(최소 통계의 첫 값 전 · 모르는 id) 0 입니다. */
        float64 getValue( const hashed_string& id ) const;
        int64   getCount( const hashed_string& id ) const { return static_cast<int64>( getValue( id ) ); }
        /** @brief 값이 한 번이라도 들어왔는가입니다(최소 통계는 0 과 "없음" 이 다르다). */
        bool hasValue( const hashed_string& id ) const;
        /** @brief 모든 값을 지웁니다(알림 없음 — 프로필을 새로 만들 때). */
        void reset();

        /** @brief 바뀜을 들을 델리게이트를 등록합니다. 푸는 데 쓰는 번호입니다(0 이 아니다). */
        uint32 registerChangeListener( const ChangeDelegate& listener );
        void   unregisterChangeListener( uint32 handle );

        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. 카탈로그에 없는 id 의 값은 알리고 버립니다. */
        [[nodiscard]] bool readState( Archive& archive );
        [[nodiscard]] bool saveToFile( string_view path ) const;
        [[nodiscard]] bool loadFromFile( string_view path );

    private:
        struct Listener
        {
            ChangeDelegate _delegate{};
            uint32         _handle{ 0 };
        };

        /** @brief 정의를 찾고 종류를 확인합니다. 맞지 않으면 경고하고 nullptr 입니다. */
        const StatDef* findDefForUse( const hashed_string& id, StatKind expectedKind, StatKind alternateKind ) const;
        /** @brief 값을 바꾸고(상한 적용) 바뀌었으면 알립니다. */
        [[nodiscard]] bool applyValue( const StatDef& def, float64 newValue );

        const StatCatalog*                    _pCatalog;
        unordered_map<hashed_string, float64> _mapValue;
        vector<Listener>                      _listListener;
        uint32                                _nextListenerHandle;
    };
} // namespace sw
