/**
 * @file GimmickCircuitDef.h
 * @brief 기믹 회로의 정의(데이터)입니다. 노드(센서, 연산자, 액추에이터이며 종류는 이름으로 지정), 매개변수, 배선을 담습니다.
 * @details 소스 엔진의 엔티티 입출력, 포탈 2 퍼즐 메이커, 마리오 메이커의 배선에 해당한다. 정의는 코드를 모르고, 종류 이름, 포트 이름,
 *          매개변수 이름의 뜻은 `GimmickNodeRegistry` 가 정한다. 회로를 만들 때(`GimmickCircuit::populate`) 알 수 없는 이름은 모두 오류이다.
 *          XML 형식은 다음과 같다. 테스트와 도구에서 쓴다.
 * @code
 *     <GimmickCircuit stepTime="0.0166667">
 *       <Node id="plate" kind="PressurePlate" threshold="50"/>
 *       <Node id="hold" kind="Pulse" seconds="3"/>
 *       <Node id="door" kind="Door" openTime="0.5" openOffset="0 3 0"/>
 *       <Wire from="plate.OnPress" to="hold.In"/>
 *       <Wire from="hold.Out" to="door.Open"/>
 *     </GimmickCircuit>
 * @endcode
 *          씬과 프리팹에서는 `GimmickCircuitComponent` 의 PROPERTY(노드와 배선 목록)가 같은 정의를 담는다.
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
    class XMLNode;

    /** @brief 매개변수 하나입니다. 이름과 적힌 문자열을 그대로 담으며, 숫자, 벡터, 이름으로 해석하는 것은 노드 종류가 정합니다. */
    struct GimmickParamDef
    {
        hashed_string _name{};
        string        _text{};
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 하나입니다. 회로 안에서 유일한 id, 종류 이름, 매개변수를 담습니다. */
    struct SW_GF_API GimmickNodeDef
    {
        hashed_string           _id{};
        hashed_string           _kind{};
        vector<GimmickParamDef> _listParam{};

        /** @brief 매개변수를 추가하거나(새 이름) 바꿉니다. */
        void setParam( const hashed_string& name, string_view text );
        /** @brief 매개변수 문자열입니다. 없으면 nullptr 입니다. */
        const string* findParam( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 배선 하나입니다. `from` 노드의 출력 포트에서 `to` 노드의 입력 포트로 이어집니다. `_bInvert` 가 켜져 있으면 값을 뒤집어서 넣습니다. */
    struct GimmickWireDef
    {
        hashed_string _fromNode{};
        hashed_string _fromPort{};
        hashed_string _toNode{};
        hashed_string _toPort{};
        uint8         _bInvert{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GimmickCircuitDef
     * @brief 노드와 배선의 목록입니다. 같은 입력 포트로 들어오는 배선이 여럿이면 OR 입니다.
     */
    class SW_GF_API GimmickCircuitDef
    {
    public:
        /** @brief 기본 고정 스텝(초)입니다. 60 Hz 입니다. */
        static constexpr float32 kDefaultStepTime = 1.0f / 60.0f;

        /** @brief 리소스 경로의 `<GimmickCircuit>` 를 읽습니다. 형식이 깨졌으면 false 입니다. 이름의 뜻은 `GimmickCircuit::populate` 가 검사합니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief XML 텍스트를 읽습니다. 테스트와 에디터에서 씁니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );

        /** @brief 노드를 추가하고 그 인덱스를 반환합니다. */
        GimmickNodeDef& addNode( const hashed_string& id, const hashed_string& kind );
        /** @brief "node.port" 형식의 문자열 두 개로 배선을 추가합니다. 점이 없으면 false 를 반환하고 추가하지 않습니다. */
        [[nodiscard]] bool addWire( string_view from, string_view to, bool bInvert = false );
        void               clear();

        /** @brief "node.port" 를 노드와 포트로 나눕니다. 점이 없거나 한쪽이 비어 있으면 false 입니다. */
        [[nodiscard]] static bool splitPortReference( string_view text, hashed_string& outNode, hashed_string& outPort );

        const GimmickNodeDef*         findNode( const hashed_string& id ) const;
        const vector<GimmickNodeDef>& getNodes() const { return _listNode; }
        const vector<GimmickWireDef>& getWires() const { return _listWire; }
        float32                       getStepTime() const { return _stepTime; }
        void                          setStepTime( float32 stepTime ) { _stepTime = stepTime; }
        const string&                 getSourceName() const { return _sourceName; }
        void                          setSourceName( string_view sourceName ) { _sourceName = string( sourceName ); }

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root );

        vector<GimmickNodeDef> _listNode{};
        vector<GimmickWireDef> _listWire{};
        string                 _sourceName{};
        float32                _stepTime{ kDefaultStepTime };
    };
} // namespace sw
