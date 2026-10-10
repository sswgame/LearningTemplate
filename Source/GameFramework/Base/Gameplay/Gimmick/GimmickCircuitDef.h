/**
 * @file GimmickCircuitDef.h
 * @brief 기믹 회로의 정의(데이터) — 노드(센서 · 연산자 · 액추에이터, 종류는 이름) · 매개변수 · 배선입니다.
 * @details 소스 엔진의 엔티티 입출력 · 포탈 2 퍼즐 메이커 · 마리오 메이커의 배선과 같은 자리입니다. 정의는 코드를 모르고, 종류 이름 · 포트 이름 ·
 *          매개변수 이름의 뜻은 `GimmickNodeRegistry` 가 정합니다 — 회로를 지을 때(`GimmickCircuit::populate`) 모르는 이름은 모두 오류입니다.
 *          XML 모양(시험 · 도구):
 * @code
 *     <GimmickCircuit stepTime="0.0166667">
 *       <Node id="plate" kind="PressurePlate" threshold="50"/>
 *       <Node id="hold" kind="Pulse" seconds="3"/>
 *       <Node id="door" kind="Door" openTime="0.5" openOffset="0 3 0"/>
 *       <Wire from="plate.OnPress" to="hold.In"/>
 *       <Wire from="hold.Out" to="door.Open"/>
 *     </GimmickCircuit>
 * @endcode
 *          씬 · 프리팹에서는 `GimmickCircuitComponent` 의 PROPERTY(노드 · 배선 목록)가 같은 정의를 담습니다.
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

    /** @brief 매개변수 하나 — 이름과 적힌 글 그대로입니다(숫자 · 벡터 · 이름의 해석은 노드 종류가 정한다). */
    struct GimmickParamDef
    {
        hashed_string _name{};
        string        _text{};
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 하나 — 회로 안에서 유일한 id, 종류 이름, 매개변수입니다. */
    struct SW_GF_API GimmickNodeDef
    {
        hashed_string           _id{};
        hashed_string           _kind{};
        vector<GimmickParamDef> _listParam{};

        /** @brief 매개변수를 더하거나(새 이름) 바꿉니다. */
        void setParam( const hashed_string& name, string_view text );
        /** @brief 매개변수 글입니다. 없으면 nullptr 입니다. */
        const string* findParam( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 배선 하나 — `from` 노드의 출력 포트에서 `to` 노드의 입력 포트로. `_bInvert` 면 뒤집어 넣습니다. */
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
        /** @brief 기본 고정 스텝(초) — 60 Hz 입니다. */
        static constexpr float32 kDefaultStepTime = 1.0f / 60.0f;

        /** @brief 리소스 경로의 `<GimmickCircuit>` 를 읽습니다. 형식이 깨졌으면 false 입니다(이름의 뜻은 `GimmickCircuit::populate` 가 본다). */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief XML 글을 읽습니다(시험 · 에디터). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );

        /** @brief 노드를 더하고 그 자리를 돌려줍니다. */
        GimmickNodeDef& addNode( const hashed_string& id, const hashed_string& kind );
        /** @brief "node.port" 두 개로 배선을 더합니다. 점이 없으면 false 이고 더하지 않습니다. */
        [[nodiscard]] bool addWire( string_view from, string_view to, bool bInvert = false );
        void               clear();

        /** @brief "node.port" 를 나눕니다. 점이 없거나 한쪽이 비면 false 입니다. */
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
