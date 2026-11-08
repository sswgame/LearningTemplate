// 머티리얼 그래프의 노드 묶음입니다. 새 노드는 여기에 한 줄을 더한다 — 컴파일러 · 메뉴 · 검사를 고치지 않는다. 문법은 docs/agents/MaterialGraph.md.
#include "pch.h"

#include "Engine/Graphics/Material/Dev/MaterialGraphNodeRegistry.h"

// clang-format off
namespace sw
{
    // 상수
    SW_MATERIAL_GRAPH_NODE( Constant,  "Constant", "", "Out:float=%Value",  "Value:float=0" );
    SW_MATERIAL_GRAPH_NODE( Constant2, "Constant", "", "Out:float2=%Value", "Value:float2=0 0" );
    SW_MATERIAL_GRAPH_NODE( Constant3, "Constant", "", "Out:float3=%Value", "Value:float3=0 0 0" );
    SW_MATERIAL_GRAPH_NODE( Constant4, "Constant", "", "Out:float4=%Value", "Value:float4=0 0 0 1" );

    // 파라미터 — 머티리얼 칸이 되어 `.material` 이 `_base` 로 값만 바꾼다(언리얼 Material Instance).
    SW_MATERIAL_GRAPH_PARAMETER_NODE( ScalarParameter, "Parameter", "", "Out:float=%Parameter", "Parameter:string=_value|Default:float=0|Min:float=0|Max:float=1", "float:Range" );
    SW_MATERIAL_GRAPH_PARAMETER_NODE( VectorParameter, "Parameter", "", "Out:float4=%Parameter|XYZ:float3=$Out.xyz", "Parameter:string=_vector|Default:float4=0 0 0 0", "float4:Float4" );
    SW_MATERIAL_GRAPH_PARAMETER_NODE( ColorParameter,  "Parameter", "", "RGBA:float4=%Parameter|RGB:float3=$RGBA.rgb|A:float=$RGBA.a", "Parameter:string=_color|Default:float4=1 1 1 1", "float4:Color" );

    // 텍스처 — 칸은 바인드리스 번호다. 핀 UV 를 잇지 않으면 메시 UV 를 쓴다.
    SW_MATERIAL_GRAPH_PARAMETER_NODE( TextureSample, "Texture", "UV:float2=context._uv", "RGBA:float4=MaterialTexture::sampleIndex( %Parameter, $UV )|RGB:float3=$RGBA.rgb|R:float=$RGBA.r|G:float=$RGBA.g|B:float=$RGBA.b|A:float=$RGBA.a", "Parameter:string=_texture|SRGB:float=1", "uint:Texture2D" );
    SW_MATERIAL_GRAPH_PARAMETER_NODE( NormalMap,     "Texture", "UV:float2=context._uv|Scale:float=1", "Out:float3=applyNormalMap( %Parameter, $Scale, $UV, context._normal, SW_MATERIAL_GRAPH_TANGENT( context ) )", "Parameter:string=_normalMap|SRGB:float=0", "uint:Texture2D" );

    // 문맥
    SW_MATERIAL_GRAPH_NODE( UV,            "Input", "", "Out:float2=context._uv", "" );
    SW_MATERIAL_GRAPH_NODE( Time,          "Input", "", "Out:float=g_SwMaterialTime", "" );
    SW_MATERIAL_GRAPH_NODE( VertexColor,   "Input", "", "Out:float4=context._color", "" );
    SW_MATERIAL_GRAPH_NODE( WorldPosition, "Input", "", "Out:float3=context._worldPosition", "" );
    SW_MATERIAL_GRAPH_NODE( WorldNormal,   "Input", "", "Out:float3=context._normal", "" );

    // 산술
    SW_MATERIAL_GRAPH_NODE( Add,      "Math", "A:any!|B:any!",            "Out:any=$A + $B", "" );
    SW_MATERIAL_GRAPH_NODE( Subtract, "Math", "A:any!|B:any!",            "Out:any=$A - $B", "" );
    SW_MATERIAL_GRAPH_NODE( Multiply, "Math", "A:any!|B:any!",            "Out:any=$A * $B", "" );
    SW_MATERIAL_GRAPH_NODE( Divide,   "Math", "A:any!|B:any!",            "Out:any=$A / $B", "" );
    SW_MATERIAL_GRAPH_NODE( Lerp,     "Math", "A:any!|B:any!|T:float=0.5", "Out:any=lerp( $A, $B, $T )", "" );
    SW_MATERIAL_GRAPH_NODE( Saturate, "Math", "In:any!",                  "Out:any=saturate( $In )", "" );
    SW_MATERIAL_GRAPH_NODE( Power,    "Math", "In:any!|Exponent:float=2", "Out:any=pow( max( $In, 0.0f ), $Exponent )", "" );
    SW_MATERIAL_GRAPH_NODE( Sine,     "Math", "In:any!",                  "Out:any=sin( $In )", "" );

    // 벡터 분해 · 결합
    SW_MATERIAL_GRAPH_NODE( Split2,   "Vector", "In:float2!", "X:float=$In.x|Y:float=$In.y", "" );
    SW_MATERIAL_GRAPH_NODE( Split3,   "Vector", "In:float3!", "X:float=$In.x|Y:float=$In.y|Z:float=$In.z", "" );
    SW_MATERIAL_GRAPH_NODE( Split4,   "Vector", "In:float4!", "X:float=$In.x|Y:float=$In.y|Z:float=$In.z|W:float=$In.w|XYZ:float3=$In.xyz", "" );
    SW_MATERIAL_GRAPH_NODE( Combine2, "Vector", "X:float=0|Y:float=0", "Out:float2=float2( $X, $Y )", "" );
    SW_MATERIAL_GRAPH_NODE( Combine3, "Vector", "X:float=0|Y:float=0|Z:float=0", "Out:float3=float3( $X, $Y, $Z )", "" );
    SW_MATERIAL_GRAPH_NODE( Combine4, "Vector", "X:float=0|Y:float=0|Z:float=0|W:float=1", "Out:float4=float4( $X, $Y, $Z, $W )", "" );

    // 표면
    SW_MATERIAL_GRAPH_NODE( Fresnel, "Surface", "Normal:float3=context._normal|Power:float=5", "Out:float=pow( saturate( 1.0f - dot( normalize( $Normal ), View::computeToView( context._worldPosition ) ) ), $Power )", "" );

    // 출력 · 하위 그래프 — 역할이 컴파일을 정한다. 출력 핀 이름은 `material_graph.hlsli` 의 `MaterialGraphOutput` 칸과 같다.
    static const MaterialGraphNodeKind sw_materialGraphNode_Output{ "Output", "Output", "BaseColor:float3!|Metallic:float=0|Roughness:float=0.5|Normal:float3=context._normal|Emissive:float3=0 0 0|Alpha:float=1", "", "ShadingModel:string=DefaultLit", "", MaterialGraphNodeRole::Output };
    static const MaterialGraphNodeKind sw_materialGraphNode_Subgraph{ "Subgraph", "Subgraph", "", "", "", "", MaterialGraphNodeRole::Subgraph };
    static const MaterialGraphNodeKind sw_materialGraphNode_GraphInput{ "GraphInput", "Subgraph", "", "Out:any=", "Name:string=In|Type:string=float", "", MaterialGraphNodeRole::GraphInput };
    static const MaterialGraphNodeKind sw_materialGraphNode_GraphOutput{ "GraphOutput", "Subgraph", "Value:any!", "", "Name:string=Out", "", MaterialGraphNodeRole::GraphOutput };
    // 층 — 노드 차례대로 출력 위에 쌓는다. 핀 Mask 를 이으면 그 값이, 아니면 MaskChannel(None · R · G · B · A, 칸 _layerMask 의 채널)이 마스크다. Weight 는 칸 _<노드>Weight 의 기본값이다.
    static const MaterialGraphNodeKind sw_materialGraphNode_Layer{ "Layer", "Layer", "Mask:float", "", "MaskChannel:string=None|Weight:float=1", "", MaterialGraphNodeRole::Layer };
} // namespace sw
// clang-format on
