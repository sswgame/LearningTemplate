/**
 * @file ElementRuleTable.h
 * @brief 원소 상호작용 규칙 테이블입니다. 재질(성질 플래그와 수치), 상태(타는 중, 대전), 자극(불, 얼음, 물, 전기를 가했을 때), 스텝 규칙(번짐, 녹임, 다 탐, 사라짐)을 정의합니다.
 * @details 야생의 숨결의 화학 엔진처럼 "불은 풀로 번지고, 물은 불을 끄고, 바람이 불을 민다" 를 코드가 아니라 데이터로 적는다. 코드에는 규칙의 **종류**
 *          (자극 동작, 스텝 규칙, 번짐 형태)만 있고, 무엇이 무엇에 닿아 무엇이 되는지는 테이블이 정한다. 알 수 없는 재질, 상태, 플래그, 자극 이름은 읽을 때 오류이다.
 *          XML 형식은 다음과 같다(`Resource/common/data/elements/default.elements.xml`).
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

#include "Engine/Asset/AssetID.h"

#include "GameFramework/GameFrameworkExports.h"

#include <optional>

namespace sw
{
    class XMLNode;

    /** @brief 상태 값이 스텝마다 어떻게 바뀌는지 나타냅니다. */
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
        [[nodiscard]] std::optional<int32> getParam( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 상태 하나입니다. 셀마다 최대 `ElementRuleTable::kMaxStatusCount` 개를 가질 수 있습니다. */
    struct ElementStatusDef
    {
        hashed_string     _id{};
        int32             _steps{ 1 }; ///< Countdown 이 붙을 때의 스텝 수
        ElementStatusKind _kind{ ElementStatusKind::Age };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 자극 규칙 하나입니다. 조건(재질, 플래그, 있어야 하는 상태, 없어야 하는 상태, 비어 있으면 아무 셀)과 동작(재질 바꾸기, 상태 붙이기와 제거, 연결된 셀로 퍼뜨리기)을 담습니다.
     * @details 자극(불, 얼음, 전기)마다 규칙을 위에서부터 보고 **처음 맞는 규칙 하나만** 적용한다. 번호는 테이블 안의 인덱스이고 -1 은 "없음" 이다.
     */
    struct ElementStimulusRule
    {
        hashed_string _event{};            ///< 바뀐 셀마다 내는 알림 이름
        int32         _material{ -1 };     ///< 조건: 이 재질
        int32         _flag{ -1 };         ///< 조건: 이 플래그가 있는 재질
        int32         _status{ -1 };       ///< 조건: 이 상태가 있어야 함
        int32         _without{ -1 };      ///< 조건: 이 상태가 없어야 함
        int32         _setMaterial{ -1 };  ///< 동작: 재질을 바꾼다
        int32         _addStatus{ -1 };    ///< 동작: 상태를 붙인다
        int32         _removeStatus{ -1 }; ///< 동작: 상태를 제거한다
        int32         _floodStatus{ -1 };  ///< 동작: 이 셀에서 `_floodThrough` 플래그 재질로 이어진(네 이웃) 셀 모두에 상태를 붙인다
        int32         _floodThrough{ -1 };
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
        hashed_string        _stepsParam{}; ///< Expire 에서 쓰는 재질 수치 이름
        int32                _status{ -1 };
        int32                _material{ -1 };    ///< Convert 에서 쓰는, 이웃의 재질
        int32                _setMaterial{ -1 }; ///< Expire, Convert 에서 쓰는, 바뀔 재질
        int32                _toFlag{ -1 };      ///< Spread 에서 쓰는, 옮겨 붙는 재질의 플래그
        int32                _after{ 1 };        ///< Spread 에서 쓰는 값. 상태가 이만큼 오래돼야 옮긴다
        ElementStepRuleKind  _kind{ ElementStepRuleKind::Spread };
        ElementSpreadPattern _pattern{ ElementSpreadPattern::Neighbors4 };
        uint8                _bCrosswind{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ElementRuleTable
     * @brief 재질, 플래그, 상태, 자극, 스텝 규칙의 테이블입니다. XML 로 읽거나 코드로 만들 수 있고 결과는 같습니다. 테이블은 바꾸지 않으므로 여러 격자가 참조해서 씁니다.
     */
    class SW_GF_API ElementRuleTable
    {
    public:
        static constexpr int32   kMaxFlagCount     = 32;
        static constexpr int32   kMaxStatusCount   = 4;
        static constexpr int32   kMaxMaterialCount = 255;
        static constexpr float32 kDefaultStepTime  = 0.25f; ///< `stepTime` 을 적지 않은 표의 스텝 길이(초)

        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );
        void               clear();
        /**
         * @brief 경로의 테이블을 한 번 읽어서 공유합니다. 오브젝트 원소 상태가 같은 테이블을 봅니다. 읽지 못했으면 nullptr 입니다. 여러 스레드에서 불러도 됩니다.
         * @details 반환한 테이블은 GameFramework 모듈을 언로드할 때까지 산다. 파일을 고치면 에디터 핫 리로드가 캐시("ElementRuleTable")로 새 테이블을 읽고
         *          `getSharedReloadCount` 를 늘린다.
         */
        static const ElementRuleTable* findShared( string_view path );
        /** @brief `findShared` 의 테이블을 다시 읽은 횟수입니다. */
        static uint32 getSharedReloadCount();

        /** @brief 플래그를 추가합니다. 이미 있으면 그 번호를 반환하고, 개수가 넘치면 -1 입니다. */
        int32 addFlag( const hashed_string& id );
        /** @brief 재질을 추가하고 재질 번호를 반환합니다. 플래그 이름은 이미 추가한 것이어야 하며, 없으면 -1 입니다. */
        int32 addMaterial( const hashed_string& id, const vector<hashed_string>& listFlag );
        /** @brief 재질 수치를 정합니다. 다 타는 스텝 수 등이 해당합니다. */
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
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool readStimulusRule( const XMLNode& node, ElementStimulusRule& outRule, string_view sourceName ) const;
        [[nodiscard]] bool readStepRule( const XMLNode& node, ElementStepRule& outRule, string_view sourceName ) const;
        /** @brief 이름 속성을 테이블의 번호로 읽습니다. 속성이 없으면 -1 로 두고 true 를 반환하고, 알 수 없는 이름이면 경고를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool readIndex( const XMLNode& node, const utf8* pAttribute, int32 ( ElementRuleTable::*pFind )( const hashed_string& ) const, int32& outIndex,
                                      string_view sourceName ) const;

        vector<hashed_string>      _listFlag{};
        vector<ElementMaterialDef> _listMaterial{};
        vector<ElementStatusDef>   _listStatus{};
        vector<ElementStimulusDef> _listStimulus{};
        vector<ElementStepRule>    _listStepRule{};
        float32                    _stepTime{ kDefaultStepTime };
    };
} // namespace sw
