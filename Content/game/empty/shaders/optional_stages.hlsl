// optional_stages.hlsl — 파이프라인 XML 의 선택 스테이지 시험용 패스 셰이더입니다. 시나리오: automation/geometrystage · tessellationstage.
// 풀스크린 삼각형을 스테이지 안에서 반으로 줄인다 — 줄어든 그림이 보이면 그 스테이지가 돈 것이다. 정점 · 픽셀 스테이지는 그대로다.
#include "binding.hlsli"
#include "fullscreen.hlsli"

float4 PSMain( PSInput input ) : SV_TARGET
{
    return float4( PassTexture::sampleSource( input._uv ).rgb, 1.0f );
}

// 지오메트리: 삼각형 하나를 받아 줄여서 낸다.
[maxvertexcount( 3 )] void GSMain( triangle PSInput arrInput[3], inout TriangleStream<PSInput> outStream )
{
    for ( uint vertexIndex = 0; vertexIndex < 3; ++vertexIndex )
    {
        PSInput output = arrInput[vertexIndex];
        output._position.xy *= 0.5f;
        outStream.Append( output );
    }
}

// 헐 · 도메인: 패치를 그대로 넘기고, 도메인이 무게 중심으로 다시 짓되 줄여서 낸다.
// 제어점의 위치는 SV_POSITION 이 아니라 일반 값으로 넘긴다 — DXC 의 SPIR-V 는 내장 위치를 `gl_PerVertex` 블록 없이 내어 GL 이 패치 사이에서 잃는다(오류 없이 아무것도 그리지 않는다).
struct ControlPoint
{
    float4 _clipPosition : TEXCOORD1;
    float2 _uv : TEXCOORD0;
};

ControlPoint VSPatchMain( uint vertexID : SV_VertexID )
{
    const PSInput fullscreen = VSMain( vertexID );
    ControlPoint  output;
    output._clipPosition = fullscreen._position;
    output._uv           = fullscreen._uv;
    return output;
}

struct PatchConstants
{
    float _arrEdge[3] : SV_TessFactor;
    float _inside : SV_InsideTessFactor;
};

PatchConstants PatchConstantMain()
{
    PatchConstants constants;
    constants._arrEdge[0] = 2.0f;
    constants._arrEdge[1] = 2.0f;
    constants._arrEdge[2] = 2.0f;
    constants._inside     = 2.0f;
    return constants;
}

[domain( "tri" )]
    [partitioning( "integer" )]
    [outputtopology( "triangle_cw" )]
    [outputcontrolpoints( 3 )]
    [patchconstantfunc( "PatchConstantMain" )]
    [maxtessfactor( 2.0f )] ControlPoint
    HSMain( InputPatch<ControlPoint, 3> patch, uint pointIndex : SV_OutputControlPointID )
{
    return patch[pointIndex];
}

    [domain( "tri" )] PSInput DSMain( PatchConstants constants, float3 barycentric : SV_DomainLocation, const OutputPatch<ControlPoint, 3> patch )
{
    PSInput output;
    output._position = patch[0]._clipPosition * barycentric.x + patch[1]._clipPosition * barycentric.y + patch[2]._clipPosition * barycentric.z;
    output._uv       = patch[0]._uv * barycentric.x + patch[1]._uv * barycentric.y + patch[2]._uv * barycentric.z;
    output._position.xy *= 0.5f;
    return output;
}
