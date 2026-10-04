/**
 * @file ElementRuleTable.h
 * @brief 원소 상호작용 규칙표 — 재질(성질 깃발 · 수치), 상태(타는 중 · 대전), 자극(불 · 얼음 · 물 · 전기를 댔을 때), 걸음 규칙(번짐 · 녹임 · 다 탐 · 사라짐)입니다.
 * @details 야생의 숨결 화학 엔진처럼 "불은 풀로 번지고, 물은 불을 끄고, 바람이 불을 민다" 를 코드가 아니라 데이터로 적습니다. 코드에는 규칙의 **종류**
 *          (자극 동작 · 걸음 규칙 · 번짐 모양)만 있고, 무엇이 무엇에 닿아 무엇이 되는지는 표가 정합니다 — 모르는 재질 · 상태 · 깃발 · 자극 이름은 읽을 때 오류입니다.
 *          XML 모양(`Resource/common/data/elements/default.elements.xml`):
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
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 상태 값이 걸음마다 어떻게 바뀌는가입니다. */
    enum class ElementStatusKind : uint8
    {
        Age = 0,  ///< 붙은 뒤 지난 걸음(0 부터 오른다, 255 에서 멈춘다) — 타는 중
        Countdown ///< 남은 걸음(붙을 때 `_steps`, 0 이면 사라진다) — 대전
    };

    /** @brief 걸음 규칙의 종류입니다. 표에 적힌 순서대로 **같은 걸음 시작 상태**를 보고 모은 뒤, 같은 순서로 적용합니다. */
    enum class ElementStepRuleKind : uint8
    {
        Expire = 0, ///< 상태가 재질 수치(`stepsParam`)만큼 오래되면 칸을 비우고 재질을 바꾼다(다 타서 맨땅)
        Convert,    ///< 상태가 있는 칸의 네 이웃 중 `neighbor` 재질을 다른 재질로(열이 얼음을 녹인다)
        Spread      ///< 상태가 `after` 걸음 지난 칸에서 이웃의 `to` 깃발 재질로 상태를 옮긴다(불이 번진다)
    };

    /** @brief 번짐 모양입니다. */
    enum class ElementSpreadPattern : uint8
    {
        Neighbors4 = 0, ///< 늘 네 이웃
        Wind            ///< 바람이 있으면 바람 쪽 한 칸(대각 포함, `crosswind` 면 바람의 양옆도), 없으면 네 이웃
    };
} // namespace sw

namespace sw
{
    /** @brief 재질 하나 — 깃발 비트와 이름 붙은 수치(다 타는 걸음 수 등)입니다. */
    struct SW_GF_API ElementMaterialDef
    {
        hashed_string         _id{};
        vector<hashed_string> _listParamName{};
        vector<int32>         _listParamValue{};
        uint32                _flags{ 0 };

        /** @brief 이름의 수치입니다. 없으면 @p fallback 입니다. */
        int32 getParam( const hashed_string& name, int32 fallback ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 상태 하나입니다(칸마다 최대 `ElementRuleTable::kMaxStatusCount` 개). */
    struct ElementStatusDef
    {
        hashed_string     _id{};
        int32             _steps{ 1 }; ///< Countdown 이 붙을 때의 걸음 수
        ElementStatusKind _kind{ ElementStatusKind::Age };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 자극 규칙 한 줄 — 조건(재질 · 깃발 · 있는 상태 · 없는 상태, 비면 아무 칸)과 동작(재질 바꾸기 · 상태 붙이기/떼기 · 이어진 칸으로 퍼뜨리기)입니다.
     * @details 자극(불 · 얼음 · 전기)마다 규칙을 위에서부터 보고 **처음 맞는 한 줄만** 적용합니다. 번호는 표 안의 자리이고 −1 은 "없음" 입니다.
     */
    struct ElementStimulusRule
    {
        hashed_string _event{};            ///< 바뀐 칸마다 내는 알림 이름
        int32         _material{ -1 };     ///< 조건: 이 재질
        int32         _flag{ -1 };         ///< 조건: 이 깃발이 있는 재질
        int32         _status{ -1 };       ///< 조건: 이 상태가 있음
        int32         _without{ -1 };      ///< 조건: 이 상태가 없음
        int32         _setMaterial{ -1 };  ///< 동작: 재질을 바꾼다
        int32         _addStatus{ -1 };    ///< 동작: 상태를 붙인다
        int32         _removeStatus{ -1 }; ///< 동작: 상태를 뗀다
        int32         _floodStatus{ -1 };  ///< 동작: 이 칸에서 `_floodThrough` 깃발 재질로 이어진(네 이웃) 칸 모두에 상태를 붙인다
        int32         _floodThrough{ -1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 자극 하나 — 이름과 규칙 줄들입니다. */
    struct ElementStimulusDef
    {
        hashed_string               _id{};
        vector<ElementStimulusRule> _listRule{};
    };
} // namespace sw

namespace sw
{
    /** @brief 걸음 규칙 한 줄입니다. 쓰지 않는 칸은 종류가 보지 않습니다. */
    struct ElementStepRule
    {
        hashed_string        _event{};
        hashed_string        _stepsParam{}; ///< Expire — 재질 수치 이름
        int32                _status{ -1 };
        int32                _material{ -1 };    ///< Convert — 이웃의 이 재질
        int32                _setMaterial{ -1 }; ///< Expire · Convert — 바뀔 재질
        int32                _toFlag{ -1 };      ///< Spread — 옮겨 붙는 재질의 깃발
        int32                _after{ 1 };        ///< Spread — 상태가 이만큼 오래돼야 옮긴다
        ElementStepRuleKind  _kind{ ElementStepRuleKind::Spread };
        ElementSpreadPattern _pattern{ ElementSpreadPattern::Neighbors4 };
        uint8                _bCrosswind{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ElementRuleTable
     * @brief 재질 · 깃발 · 상태 · 자극 · 걸음 규칙의 표입니다. XML 로 읽거나 코드로 짓습니다(같은 모양). 표는 바꾸지 않고 여러 격자가 빌려 씁니다.
     */
    class SW_GF_API ElementRuleTable
    {
    public:
        static constexpr int32 kMaxFlagCount     = 32;
        static constexpr int32 kMaxStatusCount   = 4;
        static constexpr int32 kMaxMaterialCount = 255;

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               clear();

        /** @brief 깃발을 더합니다(이미 있으면 그 번호). 넘치면 −1 입니다. */
        int32 addFlag( const hashed_string& id );
        /** @brief 재질을 더합니다. 깃발 이름은 이미 더한 것이어야 합니다(없으면 −1). 재질 번호입니다. */
        int32 addMaterial( const hashed_string& id, const vector<hashed_string>& listFlag );
        /** @brief 재질 수치를 정합니다(다 타는 걸음 수 등). */
        void  setMaterialParam( int32 material, const hashed_string& name, int32 value );
        int32 addStatus( const hashed_string& id, ElementStatusKind kind, int32 steps = 1 );
        int32 addStimulus( const hashed_string& id );
        void  addStimulusRule( int32 stimulus, const ElementStimulusRule& rule );
        void  addStepRule( const ElementStepRule& rule );

        int32 findFlag( const hashed_string& id ) const;
        int32 findMaterial( const hashed_string& id ) const;
        int32 findStatus( const hashed_string& id ) const;
        int32 findStimulus( const hashed_string& id ) const;

        bool                              hasFlag( int32 material, int32 flag ) const;
        const vector<ElementMaterialDef>& getMaterials() const { return _listMaterial; }
        const vector<ElementStatusDef>&   getStatuses() const { return _listStatus; }
        const vector<ElementStimulusDef>& getStimuli() const { return _listStimulus; }
        const vector<ElementStepRule>&    getStepRules() const { return _listStepRule; }
        float32                           getStepTime() const { return _stepTime; }
        void                              setStepTime( float32 stepTime ) { _stepTime = stepTime; }

    private:
        [[nodiscard]] bool loadRoot( const XmlNode& root, string_view sourceName );
        [[nodiscard]] bool readStimulusRule( const XmlNode& node, ElementStimulusRule& outRule, string_view sourceName ) const;
        [[nodiscard]] bool readStepRule( const XmlNode& node, ElementStepRule& outRule, string_view sourceName ) const;
        /** @brief 이름 속성을 표의 번호로 읽습니다. 속성이 없으면 −1 로 true, 모르는 이름이면 경고하고 false 입니다. */
        [[nodiscard]] bool readIndex( const XmlNode& node, const utf8* pAttribute, int32 ( ElementRuleTable::*pFind )( const hashed_string& ) const, int32& outIndex,
                                      string_view sourceName ) const;

        vector<hashed_string>      _listFlag{};
        vector<ElementMaterialDef> _listMaterial{};
        vector<ElementStatusDef>   _listStatus{};
        vector<ElementStimulusDef> _listStimulus{};
        vector<ElementStepRule>    _listStepRule{};
        float32                    _stepTime{ 0.25f };
    };
} // namespace sw
