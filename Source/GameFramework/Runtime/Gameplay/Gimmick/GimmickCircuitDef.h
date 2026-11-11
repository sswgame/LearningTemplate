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
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 회로 루트(`<GimmickCircuit stepTime>`)의 칸입니다. 회로가 지금 값(기본 스텝)으로 채운 뒤 읽는다. */
    REFLECT()
    struct SW_GF_API GimmickCircuitRootParams
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0001, Units = s )
        float32 _stepTime{ 0.0f };
    };

    /** @brief 전선(`<Wire from="node.port" to="node.port" invert/>`)의 칸입니다. */
    REFLECT()
    struct SW_GF_API GimmickWireParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _from{};
        PROPERTY( Required )
        string _to{};
        PROPERTY()
        bool _bInvert{ false }; ///< 적지 않으면 뒤집지 않는다
    };

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
        friend struct AssetTypeLoader;

    public:
        /** @brief 기본 고정 스텝(초)입니다. 60 Hz 입니다. */

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
        /** @brief 오류 글 머리에 쓰는 원본 이름입니다(없으면 `<gimmick>`). */
        string describeSource() const;
        /** @brief 오류 글 머리에 쓰는 노드 설명입니다(원본 이름 · 노드 id · 종류). */
        string describeNode( const GimmickNodeDef& node ) const;
        void   setSourceName( string_view sourceName ) { _sourceName = string( sourceName ); }

    private:
        /**
         * @brief 리소스 경로의 `<GimmickCircuit>` 를 읽습니다. 형식이 깨졌으면 false 입니다. 이름의 뜻은 `GimmickCircuit::populate` 가 검사합니다.
         * @details 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<GimmickCircuitDef>::acquire` 로 한 벌을 쥐고 회로 실행 상태(`GimmickCircuit`)는 자기가 짓는다.
         */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        [[nodiscard]] bool loadRoot( const XMLNode& root );

    private:
        [[nodiscard]] bool loadNode( const XMLNode& element, string_view sourceName );
        [[nodiscard]] bool loadWire( const XMLNode& element, string_view sourceName );

    public:
    private:
        vector<GimmickNodeDef> _listNode{};
        vector<GimmickWireDef> _listWire{};
        string                 _sourceName{};
        float32                _stepTime{ constant::time::kDefaultFixedStepSeconds };
    };

    /**
     * @brief 기믹 회로 정의를 경로마다 한 벌 나눠 줍니다("GimmickCircuitDef", 인스턴스화는 `GimmickCircuitDef.cpp`). 제자리에서 다시 읽습니다.
     * @details 회로(`GimmickCircuit::populate`)는 정의에서 노드 · 배선 상태를 지어 자기가 든다 — 같은 회로를 여러 오브젝트가 써도 정의는 한 벌이다.
     */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, GimmickCircuitDef );
} // namespace sw
