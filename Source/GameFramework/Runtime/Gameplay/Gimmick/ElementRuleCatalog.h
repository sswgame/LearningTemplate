/**
 * @file ElementRuleCatalog.h
 * @brief 원소 상호작용 규칙 테이블입니다. 재질(성질 플래그와 수치), 상태(타는 중, 대전), 자극(불, 얼음, 물, 전기를 가했을 때), 스텝 규칙(번짐, 녹임, 다 탐, 사라짐)을 정의합니다.
 * @details 야생의 숨결의 화학 엔진처럼 "불은 풀로 번지고, 물은 불을 끄고, 바람이 불을 민다" 를 코드가 아니라 데이터로 적는다. 코드에는 규칙의 **종류**
 *          (자극 동작, 스텝 규칙, 번짐 형태)만 있고, 무엇이 무엇에 닿아 무엇이 되는지는 테이블이 정한다. 알 수 없는 재질, 상태, 플래그, 자극 이름은 읽을 때 오류이다.
 *          XML 형식은 다음과 같다(`Content/common/data/elements/default.elements.xml`).
 * @code
 *     <ElementRules stepTime="0.25">
 *       <Flag id="Flammable"/><Flag id="Conductive"/><Flag id="Updraft"/>
 *       <Material id="Empty"/>
 *       <Material id="Grass" flags="Flammable,Updraft" burnSteps="4"/>
 *       <Status id="Burning" kind="Age"/>
 *       <Status id="Charged" kind="Countdown" steps="2"/>
 *       <Stimulus id="Fire">
 *         <On material="Ice" setMaterial="Water" event="Melted"/>
 *         <On flag="Flammable" without="Burning" addStatus="Burning" event="Ignited"/>
 *       </Stimulus>
 *       <Step>
 *         <Expire status="Burning" stepsParam="burnSteps" setMaterial="Empty" event="BurnedOut"/>
 *         <Convert status="Burning" neighbor="Ice" setMaterial="Water" event="Melted"/>
 *         <Spread status="Burning" after="1" to="Flammable" pattern="Wind" event="Ignited"/>
 *       </Step>
 *     </ElementRules>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 상태 값이 스텝마다 어떻게 바뀌는지 나타냅니다. */
    ENUM()
    enum class ElementStatusKind : uint8
    {
        Age = 0,  ///< 붙은 뒤 지난 스텝 수. 0 부터 늘어나고 255 에서 멈춘다. 타는 중 상태에 쓴다
        Countdown ///< 남은 스텝 수. 붙을 때 `_steps` 로 시작하고 0 이면 사라진다. 대전 상태에 쓴다
    };

    /** @brief 스텝 규칙의 종류입니다. 테이블에 적힌 순서대로 **같은 스텝 시작 상태**를 보고 모은 뒤, 같은 순서로 적용합니다. */
    enum class ElementStepRuleKind : uint8
    {
        Expire = 0, ///< 상태가 재질 수치(`stepsParam`)만큼 오래되면 셀을 비우고 재질을 바꾼다(다 타서 맨땅이 됨)
        Convert,    ///< 상태가 있는 셀의 네 이웃 중 `neighbor` 재질을 다른 재질로 바꾼다(열이 얼음을 녹임)
        Spread      ///< 상태가 `after` 스텝 지난 셀에서 이웃의 `to` 플래그 재질로 상태를 옮긴다(불이 번짐)
    };

    /** @brief 번짐 형태입니다. */
    ENUM()
    enum class ElementSpreadPattern : uint8
    {
        Neighbors4 = 0, ///< 항상 네 이웃
        Wind            ///< 바람이 있으면 바람 쪽 한 셀(대각 포함, `crosswind` 이면 바람의 양옆도), 없으면 네 이웃
    };
} // namespace sw

namespace sw
{
    /** @brief 재질 하나입니다. 플래그 비트와 이름 붙은 수치(다 타는 스텝 수 등)를 담습니다. */
    struct SW_GF_API ElementMaterialDef
    {
        hashed_string         _id{};
        vector<hashed_string> _listParamName{};
        vector<int32>         _listParamValue{};
        uint32                _flags{ 0 };

        /** @brief 이름에 해당하는 수치입니다. 이 재질이 적지 않은 이름이면 비어 있습니다(부르는 쪽이 그 뜻을 정한다). */
        [[nodiscard]] optional<int32> getParam( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 상태 하나입니다. 셀마다 최대 `ElementRuleCatalog::kMaxStatusCount` 개를 가질 수 있습니다. */
    REFLECT()
    struct SW_GF_API ElementStatusDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 1 )
        int32 _steps{ 1 }; ///< Countdown 이 붙을 때의 스텝 수
        PROPERTY()
        ElementStatusKind _kind{ ElementStatusKind::Age };
    };

    /** @brief 이름 하나만 받는 선언(`<Flag id/>` · `<Stimulus id>`)의 칸입니다. */
    REFLECT()
    struct SW_GF_API ElementNameParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
    };

    /** @brief 규칙표 루트(`<ElementRules stepTime>`)의 칸입니다. 표가 지금 값(기본 스텝)으로 채운 뒤 읽는다. */
    REFLECT()
    struct SW_GF_API ElementRuleRootParams
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0001, Units = s )
        float32 _stepTime{ 0.0f };
    };

    /** @brief 자극 규칙(`<On …/>`)의 칸입니다. 이름은 이 표에서 앞에 선언한 것만 가리키며, 읽기가 번호로 바꾼다. */
    REFLECT()
    struct SW_GF_API ElementStimulusRuleParams
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _material{};
        PROPERTY()
        hashed_string _flag{};
        PROPERTY()
        hashed_string _status{};
        PROPERTY()
        hashed_string _without{};
        PROPERTY()
        hashed_string _setMaterial{};
        PROPERTY()
        hashed_string _addStatus{};
        PROPERTY()
        hashed_string _removeStatus{};
        PROPERTY()
        hashed_string _floodStatus{};
        PROPERTY()
        hashed_string _through{};
        PROPERTY()
        hashed_string _event{};
    };

    /** @brief 스텝 규칙(`<Expire>` · `<Convert>` · `<Spread>`)의 칸입니다. 이름은 읽기가 번호로 바꾼다. */
    REFLECT()
    struct SW_GF_API ElementStepRuleParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _status{};
        PROPERTY()
        hashed_string _neighbor{};
        PROPERTY()
        hashed_string _setMaterial{};
        PROPERTY()
        hashed_string _to{};
        PROPERTY( Min = 0 )
        int32 _after{ 0 };
        PROPERTY()
        bool _bCrosswind{ false };
        PROPERTY()
        hashed_string _stepsParam{};
        PROPERTY()
        hashed_string _event{};
        PROPERTY()
        ElementSpreadPattern _pattern{ ElementSpreadPattern::Neighbors4 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 자극 규칙 하나입니다. 조건(재질, 플래그, 있어야 하는 상태, 없어야 하는 상태, 비어 있으면 아무 셀)과 동작(재질 바꾸기, 상태 붙이기와 제거, 연결된 셀로 퍼뜨리기)을 담습니다.
     * @details 자극(불, 얼음, 전기)마다 규칙을 위에서부터 보고 **처음 맞는 규칙 하나만** 적용한다. 번호는 테이블 안의 인덱스이고 비어 있으면 그 조건 · 동작이 없다.
     */
    struct ElementStimulusRule
    {
        hashed_string   _event{};        ///< 바뀐 셀마다 내는 알림 이름
        optional<int32> _material{};     ///< 조건: 이 재질
        optional<int32> _flag{};         ///< 조건: 이 플래그가 있는 재질
        optional<int32> _status{};       ///< 조건: 이 상태가 있어야 함
        optional<int32> _without{};      ///< 조건: 이 상태가 없어야 함
        optional<int32> _setMaterial{};  ///< 동작: 재질을 바꾼다
        optional<int32> _addStatus{};    ///< 동작: 상태를 붙인다
        optional<int32> _removeStatus{}; ///< 동작: 상태를 제거한다
        optional<int32> _floodStatus{};  ///< 동작: 이 셀에서 `_floodThrough` 플래그 재질로 이어진(네 이웃) 셀 모두에 상태를 붙인다
        optional<int32> _floodThrough{}; ///< `_floodStatus` 가 있으면 반드시 있다(읽을 때 본다)
    };
} // namespace sw

namespace sw
{
    /** @brief 자극 하나입니다. 이름과 규칙 목록을 담습니다. */
    struct ElementStimulusDef
    {
        hashed_string               _id{};
        vector<ElementStimulusRule> _listRule{};
    };
} // namespace sw

namespace sw
{
    /** @brief 스텝 규칙 하나입니다. 해당 종류가 쓰지 않는 필드는 읽지 않습니다. */
    struct ElementStepRule
    {
        hashed_string        _event{};
        hashed_string        _stepsParam{};  ///< Expire 에서 쓰는 재질 수치 이름
        optional<int32>      _status{};      ///< 모든 종류가 쓴다(읽을 때 반드시 있게 본다)
        optional<int32>      _material{};    ///< Convert 에서 쓰는, 이웃의 재질
        optional<int32>      _setMaterial{}; ///< Expire, Convert 에서 쓰는, 바뀔 재질(비어 있으면 0 번 재질)
        optional<int32>      _toFlag{};      ///< Spread 에서 쓰는, 옮겨 붙는 재질의 플래그
        int32                _after{ 1 };    ///< Spread 에서 쓰는 값. 상태가 이만큼 오래돼야 옮긴다
        ElementStepRuleKind  _kind{ ElementStepRuleKind::Spread };
        ElementSpreadPattern _pattern{ ElementSpreadPattern::Neighbors4 };
        uint8                _bCrosswind{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ElementRuleCatalog
     * @brief 재질, 플래그, 상태, 자극, 스텝 규칙의 테이블입니다. XML 로 읽거나 코드로 만들 수 있고 결과는 같습니다. 테이블은 바꾸지 않으므로 여러 격자가 참조해서 씁니다.
     */
    class SW_GF_API ElementRuleCatalog
    {
    public:
        /** @brief 이 데이터 종류입니다(등록은 `ElementRuleCatalog.cpp` 의 공유 캐시 줄이 로더를 더한다). 접미사가 없어 타입 있는 참조 `AssetRef<ElementRuleCatalog>` 는 경로 규칙으로 가른다. */
        static constexpr AssetTypeDef kAssetType        = AssetTypeDef::makeDataFile( "elementrules", AssetPathRule{ {}, {}, ".elements.xml" }, {} );
        static constexpr int32        kMaxFlagCount     = 32;
        static constexpr int32        kMaxStatusCount   = 4;
        static constexpr int32        kMaxMaterialCount = 255;
        static constexpr float32      kDefaultStepTime  = 0.25f; ///< `stepTime` 을 적지 않은 표의 스텝 길이(초)

        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );
        void               clear();

        /** @brief 플래그를 추가합니다. 이미 있으면 그 번호를 반환하고, 개수가 넘치면 비어 있습니다. */
        optional<int32> addFlag( const hashed_string& id );
        /** @brief 재질을 추가하고 재질 번호를 반환합니다. 겹치는 이름 · 넘치는 개수 · 추가하지 않은 플래그 이름이면 비어 있습니다. */
        optional<int32> addMaterial( const hashed_string& id, const vector<hashed_string>& listFlag );
        /** @brief 재질 수치를 정합니다. 다 타는 스텝 수 등이 해당합니다. */
        void setMaterialParam( int32 material, const hashed_string& name, int32 value );
        /** @brief 상태를 추가하고 번호를 반환합니다. 겹치는 이름이거나 개수가 넘치면 비어 있습니다. */
        optional<int32> addStatus( const hashed_string& id, ElementStatusKind kind, int32 steps = 1 );
        int32           addStimulus( const hashed_string& id );
        void            addStimulusRule( int32 stimulus, const ElementStimulusRule& rule );
        void            addStepRule( const ElementStepRule& rule );

        /** @brief 이름의 번호입니다. 없으면 비어 있습니다. */
        optional<int32> findFlag( const hashed_string& id ) const;
        optional<int32> findMaterial( const hashed_string& id ) const;
        optional<int32> findStatus( const hashed_string& id ) const;
        optional<int32> findStimulus( const hashed_string& id ) const;

        bool                              hasFlag( int32 material, int32 flag ) const;
        const vector<ElementMaterialDef>& getMaterials() const { return _listMaterial; }
        const vector<ElementStatusDef>&   getStatuses() const { return _listStatus; }
        const vector<ElementStimulusDef>& getStimuli() const { return _listStimulus; }
        const vector<ElementStepRule>&    getStepRules() const { return _listStepRule; }
        float32                           getStepTime() const { return _stepTime; }
        void                              setStepTime( float32 stepTime ) { _stepTime = stepTime; }

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool loadFlag( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadStatus( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadMaterial( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadStimulus( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadStep( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool readStimulusRule( const XMLNode& node, ElementStimulusRule& outRule, string_view sourceName ) const;
        [[nodiscard]] bool readStepRule( const XMLNode& node, ElementStepRule& outRule, string_view sourceName ) const;
        /** @brief 이름 속성을 테이블의 번호로 읽습니다. 속성이 없으면 비워 두고 true 를 반환하고, 알 수 없는 이름이면 경고를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool readIndex( const hashed_string& name, const XMLNode& node, const utf8* pAttribute, optional<int32> ( ElementRuleCatalog::*pFind )( const hashed_string& ) const,
                                      optional<int32>& outIndex,
                                      string_view      sourceName ) const;

    private:
        vector<hashed_string>      _listFlag{};
        vector<ElementMaterialDef> _listMaterial{};
        vector<ElementStatusDef>   _listStatus{};
        vector<ElementStimulusDef> _listStimulus{};
        vector<ElementStepRule>    _listStepRule{};
        float32                    _stepTime{ kDefaultStepTime };
    };

    /**
     * @brief 공유 캐시("elementrules")는 GameFramework 가 인스턴스화합니다(`ElementRuleCatalog.cpp`). 쓰는 쪽은 `SharedAssetCache<ElementRuleCatalog>::acquire` 로 받습니다(같은 경로는 같은 객체).
     * @details 파일을 고치면 에디터 핫 리로드가 캐시에서 떼어 내고 `getReloadCount` 를 늘린다 — 쥔 쪽은 옛 테이블을 쥐고 있다가 다시 받는다.
     */
    extern template class SW_GF_API_EXTERN_TEMPLATE SharedAssetCache<ElementRuleCatalog>;
} // namespace sw
