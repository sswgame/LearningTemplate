/**
 * @file WeatherSystem.h
 * @brief 날씨 — 계절마다 가중치로 다음 날씨를 고르고, 정한 시간 동안 이어지다 부드럽게 넘어가며, 젖음 · 바람 · 안개 같은 값을 섞어 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    class XmlNode;

    /** @brief 계절 하나의 가중치입니다. */
    struct WeatherSeasonWeight
    {
        hashed_string _season{};
        float32       _weight{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 날씨 한 종류입니다. */
    struct SW_GF_API WeatherDef
    {
        hashed_string               _id{};
        vector<WeatherSeasonWeight> _listSeasonWeight{}; ///< 비면 모든 계절에 `_weight`
        StatBlock                   _values{};           ///< `<Values wetness="1" wind="0.6" fog="0.2"/>` — 렌더 · 게임 규칙이 읽는다
        float32                     _weight{ 1.0f };
        float32                     _minDuration{ 600.0f }; ///< 게임 초
        float32                     _maxDuration{ 1800.0f };

        float32 computeWeight( const hashed_string& season ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class WeatherCatalog
     * @brief `<WeatherCatalog transition="60"><Weather id="rain" weight="1" seasons="Spring:3,Summer:1" minDuration="600" maxDuration="1800">
     *        <Values wetness="1" wind="0.4"/></Weather></WeatherCatalog>` 를 읽습니다.
     */
    class SW_GF_API WeatherCatalog : public XmlCatalog<WeatherCatalog>
    {
        friend class XmlCatalog<WeatherCatalog>;

    public:
        const WeatherDef*         findWeather( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<WeatherDef>& getWeathers() const { return _catalog.getAll(); }
        float32                   getTransitionTime() const { return _transitionTime; }

    private:
        static constexpr const utf8* kXmlRootName = "WeatherCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<WeatherDef> _catalog{};
        float32                 _transitionTime{ 60.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class WeatherSystem
     * @brief 지금 날씨와 넘어가는 중인 이전 날씨입니다. 시간은 게임 초(`WorldClock` 과 같은 배율)로 넘깁니다. 씨앗이 같으면 같은 날씨가 이어집니다.
     */
    class SW_GF_API WeatherSystem
    {
    public:
        WeatherSystem();

        void initialize( const WeatherCatalog* pCatalog, uint32 seed, const hashed_string& season );
        /** @brief 시간을 흘립니다. 날씨가 바뀌었으면 true 입니다. */
        bool update( float32 gameSeconds, const hashed_string& season );
        /** @brief 날씨를 정합니다(이벤트 · 퀘스트). @p duration 0 이면 정의의 범위에서 고른다. */
        void forceWeather( const hashed_string& weatherId, float32 duration = 0.0f, bool bImmediate = false );
        /** @brief 앞으로 올 날씨 @p count 개를 미리 봅니다(일기 예보 — 상태를 바꾸지 않는다). */
        void forecast( const hashed_string& season, int32 count, vector<hashed_string>& outListWeather ) const;

        /** @brief 이전 → 지금 섞은 값입니다(넘어가는 중이면 사이). */
        float32       computeValue( const hashed_string& name ) const;
        hashed_string getCurrent() const { return _pCurrent != nullptr ? _pCurrent->_id : hashed_string{}; }
        hashed_string getPrevious() const { return _pPrevious != nullptr ? _pPrevious->_id : hashed_string{}; }
        /** @brief 넘어간 정도(0 = 이전 그대로, 1 = 지금 날씨)입니다. */
        float32 getBlend() const;
        float32 getRemaining() const { return _remaining; }

    private:
        const WeatherDef* pickWeather( const hashed_string& season, GameRandom& random, const WeatherDef* pExclude ) const;
        float32           pickDuration( const WeatherDef& weather, GameRandom& random ) const;

        const WeatherCatalog* _pCatalog;
        const WeatherDef*     _pCurrent;
        const WeatherDef*     _pPrevious;
        GameRandom            _random;
        float32               _remaining;
        float32               _transitionElapsed;
    };
} // namespace sw
