/**
 * @file DecalComponent.h
 * @brief 디퍼드 데칼(길 · 손상 · 때)입니다 — 상자 모양 투영으로 G버퍼의 알베도 · 노멀 · 머티리얼 칸을 마스크대로 덮습니다(언리얼 Deferred Decal · 유니티 HDRP Decal Projector).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    /**
     * @enum DecalChannels
     * @brief 데칼이 덮는 G버퍼 칸입니다(비트). 언리얼 DBuffer 의 Color · Normal · Roughness 마스크와 같은 뜻이다.
     */
    ENUM( Flags )
    enum class DecalChannels : uint8
    {
        None     = 0,
        Albedo   = SW_BIT( 0 ), ///< 알베도 rgb(셰이딩 모델 번호 알파는 건드리지 않는다)
        Normal   = SW_BIT( 1 ), ///< 노멀(노멀 텍스처가 있을 때만 — 없으면 끈다)
        Material = SW_BIT( 2 ), ///< 금속도 · 거칠기(반사율 · 가림은 그대로)
        All      = Albedo | Normal | Material,
    };

    /**
     * @struct DecalDrawData
     * @brief 데칼 하나의 그리기 값입니다(값 타입 — 게임 스레드가 패킷에 싣고 렌더 스레드가 읽는다). 텍스처는 경로로 싣고 렌더 스레드가 읽는다(`FramePostProcessTextureSet` 와 같은 길).
     */
    struct DecalDrawData
    {
        float4x4 _world{};                         ///< 상자(-0.5..0.5)의 월드 행렬 — 행 0 · 1 · 2 가 상자 축(1 이 투영 축 — 아래를 본다)
        float4x4 _worldToLocal{};                  ///< 그 역행렬
        float4   _color{ 1.0f, 1.0f, 1.0f, 1.0f }; ///< 알베도 곱 · 불투명도(a)
        float4   _surface{};                       ///< x 거칠기, y 금속도, z 노멀 세기, w 각도 페이드 시작(투영 축과 면 노멀의 cos)
        DataPath _albedoTexture;                   ///< rgb 색 · a 덮임(없으면 흰색)
        DataPath _normalTexture;                   ///< 탄젠트 공간 노멀(데칼 X · -Z 축). 없으면 노멀 칸을 덮지 않는다
        int32    _sortOrder{ 0 };                  ///< 작은 것부터 그린다(겹치면 큰 것이 위)
        uint8    _channels{ static_cast<uint8>( DecalChannels::All ) };
    };

    /**
     * @class DecalComponent
     * @brief 디퍼드 데칼 컴포넌트입니다. 컴포넌트 변환이 상자(-0.5..0.5 의 단위 상자 × 크기)이고, 로컬 -Y 쪽으로 투영합니다(기본 자세로 땅에 길 · 때를 깐다).
     * @details 디퍼드 파이프라인의 `Decal` 패스(G버퍼 뒤 · 조명 앞)가 그린다 — 알베도 · 노멀 · 머티리얼 칸을 데칼 불투명도 × 텍스처 덮임 × 각도 · 끝 페이드로 섞는다.
     *          포워드 파이프라인은 그리지 않는다 — 덮을 G버퍼가 없다(언리얼처럼 DBuffer 를 두면 모든 포워드 머티리얼 셰이더가 데칼 버퍼 셋을 읽어야 한다. 렌더러가 한 번 경고한다).
     *          움직이는 메시 위에서는 미끄러진다(데칼은 투영이다 — 언리얼 Receives Decals 를 끄는 자리는 아직 없다).
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Decal", Tooltip = "Projects albedo, normal and roughness onto the G-buffer inside its box (deferred pipeline)", Meta = "Icon=Cube" )
    class SW_API DecalComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        DecalComponent();
        virtual ~DecalComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 그리기 값을 채웁니다(게임 스레드 — 패킷 수집). 노멀 텍스처가 없으면 노멀 칸을 끈다. */
        void writeDrawData( DecalDrawData& outData ) const;

    private:
        PROPERTY( Tooltip = "Albedo texture (rgb colour, a coverage); empty is white" )
        DataPath _albedoTexture;
        PROPERTY( Tooltip = "Tangent space normal texture (decal X and -Z axes); empty leaves the normal untouched" )
        DataPath _normalTexture;
        PROPERTY( Tooltip = "Albedo multiplier (rgb) and opacity (a)" )
        float4 _color;
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Roughness written to the G-buffer" )
        float32 _roughness;
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Metallic written to the G-buffer" )
        float32 _metallic;
        PROPERTY( Min = 0.0, Max = 4.0, Tooltip = "Normal texture strength" )
        float32 _normalStrength;
        PROPERTY( Min = -1.0, Max = 1.0, Tooltip = "Cosine between the projection axis and the surface normal where the decal starts to fade (steep walls fade out)" )
        float32 _fadeStartCosine;
        PROPERTY( Tooltip = "Draw order; larger draws on top" )
        int32 _sortOrder;
        PROPERTY( Tooltip = "G-buffer channels the decal covers" )
        DecalChannels _channels;
    };
} // namespace sw
