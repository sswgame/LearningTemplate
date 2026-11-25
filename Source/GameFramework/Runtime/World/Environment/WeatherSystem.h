/**
 * @file WeatherSystem.h
 * @brief 날씨를 정의합니다.
 * @details 계절마다 가중치로 다음 날씨를 고르고, 정한 시간 동안 이어지다가 부드럽게 넘어간다. 젖음, 바람, 안개 같은 값을 섞어 준다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    class Archive;
    class XMLNode;
    struct MaterialParameterCollectionDef;

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
    REFLECT()
    struct SW_GF_API WeatherDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string               _id{};
        vector<WeatherSeasonWeight> _listSeasonWeight{}; ///< 비면 모든 계절에 `_weight` — `seasons="Spring:3,Summer:1"` 은 손으로 푼다
        StatBlock                   _values{};           ///< `<Values wetness="1" wind="0.6" fog="0.2"/>` . 렌더러와 게임 규칙이 읽는다
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
        PROPERTY( Min = 1.0, Units = s ) ///< 0 이면 한 프레임마다 날씨가 바뀐다
        float32 _minDuration{ 600.0f };  ///< 게임 초
        PROPERTY( Min = 1.0, Units = s ) ///< `minDuration` 이상이어야 한다(읽기가 본다)
        float32 _maxDuration{ 1800.0f };

        float32 computeWeight( const hashed_string& season ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 머티리얼 파라미터 묶음 칸 하나와 그 채널마다의 날씨 값 이름입니다(`<MaterialBinding parameter="Weather" valueX="wetness" valueY="puddle"/>`).
     * @details 비운 채널은 묶음 파일의 값 그대로다(설선 높이처럼 날씨가 정하지 않는 값). 판 상태의 쓰개(`WeatherMaterialWriter`)가 틱마다 쓴다.
     */
    REFLECT()
    struct SW_GF_API WeatherMaterialBinding
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Parameter of the material parameter collection the weather writes" )
        hashed_string _parameter{};
        PROPERTY( Tooltip = "Weather value written to x (empty: the file value)" )
        hashed_string _valueX{};
        PROPERTY( Tooltip = "Weather value written to y (empty: the file value)" )
        hashed_string _valueY{};
        PROPERTY( Tooltip = "Weather value written to z (empty: the file value)" )
        hashed_string _valueZ{};
        PROPERTY( Tooltip = "Weather value written to w (empty: the file value)" )
        hashed_string _valueW{};
    };
} // namespace sw

namespace sw
{
    /** @brief 날씨가 배율로 움직이는 씬의 값입니다 — 배율은 씬 데이터에 적은 값에 곱합니다. */
    ENUM()
    enum class WeatherSceneTarget : uint8
    {
        SunIntensity = 0, ///< 켜진 방향광의 세기(`LightComponent::setIntensityScale`)
        SkyIntensity,     ///< 하늘의 세기 — 스카이 라이트 · 간접광도 같은 값을 본다(`SkyComponent::setIntensityScale`)
        FogDensity,       ///< 안개의 밀도(`FogComponent::setDensityScale`)
        WindStrength,     ///< 흔들림 세기 — 식생(`WindComponent::setStrengthScale`)
        WindSpeed,        ///< 풍속 — 돛 · 천의 힘(`WindComponent::setSpeedScale`)
    };

    /**
     * @brief 날씨 값 하나가 씬의 값 하나를 몇 배로 만드는지입니다(`<SceneBinding target="SunIntensity" value="skyLight" atZero="0.1" atOne="1"/>`).
     * @details 배율은 날씨 값 0 에서 `atZero`, 1 에서 `atOne` 이고 그 사이는 직선이다(0..1 밖은 자른다). 같은 대상에 여럿이면 곱한다. 언리얼의 날씨 블루프린트가 해 · 하늘 · 안개 · 바람을
     *          한 자산의 값으로 돌리는 자리다 — 여기서는 카탈로그가 그 표다(장면마다 잇는 컴포넌트를 놓지 않는다).
     */
    REFLECT()
    struct SW_GF_API WeatherSceneBinding
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Scene value the weather scales" )
        WeatherSceneTarget _target{ WeatherSceneTarget::SunIntensity };
        PROPERTY( Required, Tooltip = "Weather value (<Values>) that drives the scale" )
        hashed_string _value{};
        PROPERTY( Min = 0.0, Tooltip = "Scale while the weather value is 0" )
        float32 _atZero{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Scale while the weather value is 1" )
        float32 _atOne{ 1.0f };
    };

    /**
     * @brief 날씨 값 하나가 내는 반복 소리입니다(`<SoundBinding event="Weather.Rain" value="rain" parameter="WeatherRain" opennessParameter="WeatherOpenness"/>`).
     * @details 값이 0 보다 큰 동안 이벤트(반복, 2D)를 내고 값을 게임 파라미터(RTPC)로 건다 — 크기 · 음색은 이벤트 라이브러리의 곡선이 정한다. 듣는 자리의 하늘 열림(0 실내 .. 1 트임,
     *          `WeatherShelter`)도 파라미터로 걸어 실내에서 작고 먹먹하게 만든다.
     */
    REFLECT()
    struct SW_GF_API WeatherSoundBinding
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Looping audio event that plays while the weather value is above 0" )
        hashed_string _event{};
        PROPERTY( Required, Tooltip = "Weather value (<Values>) that starts the sound and drives its parameter" )
        hashed_string _value{};
        PROPERTY( Required, Tooltip = "Audio parameter (RTPC) that receives the weather value" )
        hashed_string _parameter{};
        PROPERTY( Tooltip = "Audio parameter (RTPC) that receives the sky openness at the listener, 0 indoors .. 1 open sky (empty: the sound ignores roofs)" )
        hashed_string _opennessParameter{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class WeatherCatalog
     * @brief 날씨 카탈로그(`<WeatherCatalog>`)를 읽습니다.
     * @details `materialCollection` 과 `<MaterialBinding>` 은 날씨 값을 지면 머티리얼이 읽는 파라미터 묶음 칸에 잇는다 — 날씨를 연 판 상태가 늘 쓴다(`WeatherMaterialWriter`).
     *          바인딩이 있는데 묶음이 없으면 읽기 오류다. `<SceneBinding>` 은 해 · 하늘 · 안개 · 바람을, `<SoundBinding>` 은 빗소리 · 바람 소리를 날씨에 잇는다(`WeatherSceneWriter`).
     * @code
     *     <WeatherCatalog transition="60" materialCollection="guid:…">
     *         <MaterialBinding parameter="Weather" valueX="wetness"/>
     *         <Weather id="rain" weight="1" seasons="Spring:3,Summer:1" minDuration="600" maxDuration="1800">
     *         <Values wetness="1" wind="0.4"/></Weather></WeatherCatalog>
     * @endcode
     */
    /** @brief 날씨 표 루트(`<Weathers transition>`)의 칸입니다. */
    REFLECT()
    struct SW_GF_API WeatherCatalogRootParams
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Units = s )
        float32 _transition{ 0.0f };
        PROPERTY( Tooltip = "Material parameter collection (*.matparams.xml) the <MaterialBinding> rows write (empty: the weather drives no materials)" )
        AssetRef<MaterialParameterCollectionDef> _materialCollection;

        WeatherCatalogRootParams();
    };

    class SW_GF_API WeatherCatalog : public GameCatalog<WeatherDef, XMLCatalog<WeatherCatalog>>
    {
        friend class XMLCatalog<WeatherCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<WeatherCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".weather.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `WeatherSystem.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "WeatherCatalog", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };

        float32 getTransitionTime() const { return _transitionTime; }
        /** @brief 바인딩이 쓰는 머티리얼 파라미터 묶음 경로입니다(없으면 비어 있다). */
        const string& getMaterialCollection() const { return _materialCollection; }
        /** @brief 날씨 값 → 묶음 칸 바인딩입니다(`<MaterialBinding>`). */
        const vector<WeatherMaterialBinding>& getMaterialBindings() const { return _listMaterialBinding; }
        /** @brief 날씨 값 → 씬 값 배율 바인딩입니다(`<SceneBinding>`). */
        const vector<WeatherSceneBinding>& getSceneBindings() const { return _listSceneBinding; }
        /** @brief 날씨 값 → 반복 소리 바인딩입니다(`<SoundBinding>`). */
        const vector<WeatherSoundBinding>& getSoundBindings() const { return _listSoundBinding; }

    private:
        static constexpr const utf8* kXMLRootName = "WeatherCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadWeather( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadMaterialBinding( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadSceneBinding( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadSoundBinding( const XMLNode& node, string_view sourceName );

    private:
        float32                        _transitionTime{ 60.0f };
        string                         _materialCollection{};
        vector<WeatherMaterialBinding> _listMaterialBinding{};
        vector<WeatherSceneBinding>    _listSceneBinding{};
        vector<WeatherSoundBinding>    _listSoundBinding{};
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<WeatherCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, WeatherCatalog );
} // namespace sw

namespace sw
{
    /**
     * @class WeatherSystem
     * @brief 지금 날씨와 넘어가는 중인 이전 날씨를 보관합니다. 시간은 게임 초(`WorldClock` 과 같은 배율)로 넘기며, 시드가 같으면 같은 날씨가 이어집니다.
     */
    REFLECT()
    class SW_GF_API WeatherSystem
    {
    public:
        REFLECT_BODY();

        static constexpr FormatDef kStateFormat{ "WTHR" };

        WeatherSystem();

        void initialize( const WeatherCatalog* pCatalog, uint32 seed, const hashed_string& season );
        /** @brief 시간을 흘립니다. 날씨가 바뀌었으면(날씨 없음에서 시작한 경우 포함) true 입니다. */
        bool update( float32 gameSeconds, const hashed_string& season );
        /** @brief 날씨를 정합니다. 이벤트와 퀘스트가 씁니다. @p duration 이 0 이면 정의의 범위에서 고릅니다. */
        void forceWeather( const hashed_string& weatherID, float32 duration = 0.0f, bool bImmediate = false );
        /** @brief 앞으로 올 날씨 @p count 개를 미리 봅니다. 일기 예보이며 상태는 바꾸지 않습니다. */
        void forecast( const hashed_string& season, int32 count, vector<hashed_string>& outListWeather ) const;

        /** @brief 이전 날씨와 지금 날씨를 섞은 값입니다. 넘어가는 중이면 그 사이 값입니다. */
        float32       computeValue( const hashed_string& name ) const;
        hashed_string getCurrent() const { return _pCurrent != nullptr ? _pCurrent->_id : hashed_string{}; }
        hashed_string getPrevious() const { return _pPrevious != nullptr ? _pPrevious->_id : hashed_string{}; }
        /** @brief 넘어간 정도(0 = 이전 그대로, 1 = 지금 날씨)입니다. */
        float32 getBlend() const;
        float32 getRemaining() const { return _remaining; }

        /**
         * @brief 남은 시간, 전환 경과(리플렉션 상태 칸)와 지금 · 이전 날씨 id, 난수(손 본문 — 정의 포인터 ↔ id, `GameRandom`)를 씁니다. 카탈로그는 `initialize` 의 것이라 싣지 않습니다.
         *        `writeState` 의 바이트로 상태를 바꿉니다. 날씨 id 가 지금 카탈로그에 없거나 바이트가 깨졌으면 false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        const WeatherDef* pickWeather( const hashed_string& season, GameRandom& random, const WeatherDef* pExclude ) const;
        float32           pickDuration( const WeatherDef& weather, GameRandom& random ) const;

    private:
        const WeatherCatalog* _pCatalog;
        const WeatherDef*     _pCurrent;
        const WeatherDef*     _pPrevious;
        GameRandom            _random;
        PROPERTY( RuntimeState )
        float32 _remaining;
        PROPERTY( RuntimeState )
        float32 _transitionElapsed;
    };
} // namespace sw
