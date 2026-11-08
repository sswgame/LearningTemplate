/**
 * @file MaterialGraph.h
 * @brief 머티리얼 그래프 저작 데이터(`*.matgraph.xml`)입니다. 노드 · 연결 · 값 · 캔버스 위치만 담고, 노드 종류의 뜻은 등록표(`MaterialGraphNodeRegistry`)가 정합니다.
 * @details 언리얼 Material Editor · 유니티 Shader Graph · Godot Visual Shader 의 저작 형식 자리입니다(비교는 기억에 의존).
 *          - **개발 전용이다.** `Dev/` 폴더라 배포 구성은 짓지 않고, 팩도 이 파일을 담지 않는다(`Config/Engine/PackConfig.json`). 런타임은 컴파일이 낸 `.hlsl` · `.gen.material` 만 읽는다.
 *          - **연결의 기준은 받는 노드의 입력 목록이다**(`_listLink` — 소리 그래프와 같은 선). 링크 표를 따로 두면 두 표가 어긋난다. 입력 핀 하나는 링크 하나만 받는다.
 *          - **변형.** 루트의 `_base` 와 열쇠(`_name` · `_pin`)로 부모 그래프의 차이만 적는다(`docs/agents/DataInheritance.md`). 값만 바꾸는 변형은 생성된 머티리얼의 `_base`(언리얼 Material Instance)가 먼저다 — 그래프 변형은 셰이더가 달라지는 경우에 쓴다.
 * @code
 *     <MaterialGraphDef formatVersion="0">
 *         <_listNode>
 *             <MaterialGraphNodeDef _name="Tint" _kind="ColorParameter" _position="0,0">
 *                 <_listValue><MaterialGraphValue _name="Parameter" _value="_tint" /></_listValue>
 *             </MaterialGraphNodeDef>
 *             <MaterialGraphNodeDef _name="Out" _kind="Output" _position="300,0">
 *                 <_listLink><MaterialGraphLink _pin="BaseColor" _fromNode="Tint" _fromPin="RGB" /></_listLink>
 *             </MaterialGraphNodeDef>
 *         </_listNode>
 *     </MaterialGraphDef>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/GraphCommentBox.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ResourceContext;

    /** @brief 받는 노드의 입력 핀 하나에 이어진 출력 핀입니다. */
    REFLECT()
    struct SW_API MaterialGraphLink
    {
        REFLECT_BODY();

        PROPERTY( InheritKey, Tooltip = "Input pin of the node that reads" )
        string _pin{};
        PROPERTY( Tooltip = "Node that writes" )
        string _fromNode{};
        PROPERTY( Tooltip = "Output pin of that node" )
        string _fromPin{};
    };
} // namespace sw

namespace sw
{
    /** @brief 노드의 값 하나입니다. 이름이 설정 칸이면 그 값이고, 입력 핀이면 잇지 않았을 때의 상수입니다(언리얼 노드의 인라인 상수). */
    REFLECT()
    struct SW_API MaterialGraphValue
    {
        REFLECT_BODY();

        PROPERTY( InheritKey, Tooltip = "Setting or input pin name the node kind declares" )
        string _name{};
        PROPERTY( Tooltip = "Text of the value: numbers split by spaces or commas, or a name" )
        string _value{};
    };
} // namespace sw

namespace sw
{
    /** @brief 그래프의 노드 하나입니다. */
    REFLECT()
    struct SW_API MaterialGraphNodeDef
    {
        REFLECT_BODY();

        PROPERTY( InheritKey, Tooltip = "Node name links refer to; unique in the graph" )
        string _name{};
        PROPERTY( Tooltip = "Registered node kind (MaterialGraphNodeRegistry)" )
        string _kind{};
        PROPERTY( Tooltip = "Connected input pins" )
        vector<MaterialGraphLink> _listLink{};
        PROPERTY( Tooltip = "Settings and constants of unconnected pins; values left out keep the kind's default" )
        vector<MaterialGraphValue> _listValue{};
        PROPERTY( AssetPath, Tooltip = "Asset of the node: the default texture of a texture parameter, the graph of a subgraph node" )
        string _assetPath{};
        PROPERTY( Tooltip = "Position on the editor canvas; the compiler does not read it" )
        float2 _position{ 0.0f, 0.0f };

        /** @brief 값 @p name 입니다. 적지 않았으면 nullptr 입니다. */
        [[nodiscard]] const MaterialGraphValue* findValue( string_view name ) const;
        /** @brief 입력 핀 @p pin 의 링크입니다. 잇지 않았으면 nullptr 입니다. */
        [[nodiscard]] const MaterialGraphLink* findLink( string_view pin ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 머티리얼 그래프 파일 하나입니다(`*.matgraph.xml`).
     * @details 출력 노드(`Output`)가 있으면 머티리얼이 되고, `GraphOutput` 이 있으면 다른 그래프가 펼쳐 넣는 하위 그래프입니다(언리얼 Material Function).
     */
    REFLECT()
    struct SW_API MaterialGraphDef
    {
        REFLECT_BODY();
        /** @brief 그래프 파일의 접미사입니다. */
        static constexpr string_view kExtension = ".matgraph.xml";
        /** @brief 이 에셋 종류의 접미사입니다(그대로 읽는 저작 파일 — 쿠킹본이 아니라 생성물이 런타임으로 간다). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 머티리얼 그래프 에셋 종류입니다(등록은 `MaterialGraph.cpp` 의 `SW_ASSET_TYPE`). 판 0. */
        static constexpr AssetTypeDef kAssetType{ "MaterialGraph", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        PROPERTY( Name = "formatVersion", Tooltip = "Schema version of the file" )
        uint32 _formatVersion{ 0 };
        PROPERTY( Tooltip = "Nodes of the graph" )
        vector<MaterialGraphNodeDef> _listNode{};
        PROPERTY( Tooltip = "Comment boxes of the graph editor; the compiler does not read them" )
        vector<GraphCommentBox> _listComment{};

        /** @brief 리소스 경로의 파일을 읽습니다(`_base` 를 펼친다). 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief XML 글을 읽습니다(`_base` 를 펼친다). 모르는 칸 · 다른 판은 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );

    private:
        /** @brief 합성 문맥을 받는 읽기의 본문입니다. `loadFromAsset` 은 리소스 id 를 실은 문맥(`withAsset`)을, 글 입구는 id 없는 문맥을 넘깁니다(덮어쓰기 층 · 다시 읽기 기록 · 접속 대조는 id 로 읽을 때만 받는다). */
        [[nodiscard]] bool loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context );

    public:
        /** @brief 모든 칸을 적은 글입니다(되돌리기 기록 · 복사 버퍼). `loadFromXMLText` 가 다시 읽습니다. */
        [[nodiscard]] string saveToXMLText() const;
        /** @brief 파일에 씁니다. @p baseID 가 있으면 그 부모와 다른 칸만 적습니다(`ReflectedXMLFile::saveDescAsChild`). */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath, const AssetID& baseID ) const;
        /** @brief 이름의 노드 번호입니다. 없으면 비어 있습니다. */
        [[nodiscard]] optional<uint32> findNodeIndex( string_view name ) const;
    };
} // namespace sw
