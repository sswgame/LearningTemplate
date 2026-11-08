/**
 * @file SkinLayer.h
 * @brief 피부 층 쌓기(화장 · 문신 · 주근깨 · 흉터 · 바디 페인트)입니다. 층 종류(`skinlayers.xml`)와 플레이어가 고른 층 목록(순서 · 색 · 불투명도 · 배치)을 UV 공간에서 합칩니다.
 * @details **합치기는 캐릭터마다 한 번 굽는다.** 층 목록이 바뀔 때만 GPU 가 UV 공간 텍스처 한 장(알베도 · 거칠기 · 금속 덮어쓰기)에 차례로 그리고, 피부 머티리얼은 그 한 장만 읽는다 —
 *          층이 열 개를 넘는 화장에서 픽셀마다 층을 다 읽으면 비싸서다(언리얼 MetaHuman · 검은사막은 굽는 쪽, inZOI 는 굽는 쪽에 데칼 몇 장). 굽기 패스는 `skin_layer_composite.hlsli` 의
 *          함수를 쓰고, 여기의 CPU 함수(`SkinSurfaceSample`)는 같은 식의 기준 구현이다(테스트 · 미리보기 섬네일).
 *          층 하나 = 마스크 텍스처(종류가 정함) × 색(견본 묶음 — 염색과 같은 팔레트) × 불투명도, 섞기(보통 · 곱하기 · 오버레이), 머티리얼 덮어쓰기(립스틱 광 = 거칠기, 펄 = 금속).
 *          문신 · 데칼은 배치(UV 이동 · 회전 · 크기 · 좌우 대칭)가 있고 영역 경계(`regionMin` · `regionMax`) 밖은 자른다 — UV 이음새를 넘어가 번지지 않게.
 *          **번짐**은 층마다 실행 상태(`SkinLayerSmearState`)다. 체액 흐름(눈물 · 땀 · 비)과 접촉(손으로 닦기 · 맞음 · 먹기)이 색을 끌고 묽게 하며, 층 종류의 번짐 저항(방수 마스카라)이 줄인다.
 *          번짐은 선택 · 공유 코드에 싣지 않는다(일시 상태). 씻기가 지운다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceLoadReport;
    class XMLNode;

    /** @brief 층이 아래 색과 섞이는 방법입니다(`skin_layer_composite.hlsli` 와 같은 식). */
    ENUM()
    enum class SkinLayerBlend : uint8
    {
        Normal = 0, ///< lerp(아래, 층, α)
        Multiply,   ///< 아래 × 층 — 그림자 · 문신 잉크
        Overlay     ///< 오버레이 — 홍조 · 주근깨가 피부 결을 살린다
    };

    /** @brief 층 종류 하나입니다(`<SkinLayer id category mask blend …/>`). */
    REFLECT()
    struct SW_GF_API SkinLayerDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _category{}; ///< 편집 창 묶음과 부분 프리셋 카테고리(Makeup · Tattoo · Scar · Paint)
        PROPERTY( AssetPath, Required )
        hashed_string _mask{}; ///< 마스크 텍스처(R = 덮임). 배치가 없는 층은 피부 UV 그대로, 있는 층은 층 UV [0,1]
        PROPERTY()
        hashed_string _palette{}; ///< 색 견본 묶음(`customization.xml` 의 `<Palette>`). 비어 있으면 자유 색
        PROPERTY()
        SkinLayerBlend _blend{ SkinLayerBlend::Normal };
        float4         _defaultColor{ 1.0f, 1.0f, 1.0f, 1.0f }; ///< `defaultColor="r g b a"`(손으로 읽는다)
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _defaultOpacity{ 1.0f };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _roughness{ 0.5f };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _roughnessWeight{ 0.0f }; ///< 거칠기를 층 값으로 바꾸는 정도(0 = 덮지 않음, 립스틱 광은 1)
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _metallic{ 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _metallicWeight{ 0.0f }; ///< 금속을 층 값으로 바꾸는 정도(펄 · 반짝이)
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _smearResistance{ 0.0f }; ///< 번짐 저항(1 = 방수 마스카라 · 문신, 0 = 바로 번지는 립스틱)
        float2  _regionMin{ 0.0f, 0.0f }; ///< 피부 UV 경계(`regionMin="u v"`) — 밖은 자른다
        float2  _regionMax{ 1.0f, 1.0f };
        PROPERTY( Name = "placeable" )
        bool _bPlaceable{ false }; ///< 배치(이동 · 회전 · 크기 · 대칭)를 받는지(문신 · 데칼)

        /** @brief 피부 UV 가 영역 경계 안이면 true 입니다. */
        bool isInsideRegion( const float2& skinUV ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 플레이어가 고른 층 하나입니다(선택 · 공유 코드에 실린다). 목록 순서가 그리기 순서(뒤가 위)다.
     * @details 값은 전송 정밀도로 맞춘 뒤(`quantize`) 쓴다 — 보낸 쪽과 받은 쪽의 굽기 결과 · 해시가 같다(꾸미기 값과 같은 규칙).
     */
    struct SW_GF_API SkinLayerValue
    {
        float4        _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float2        _offset{ 0.5f, 0.5f }; ///< 층 가운데의 피부 UV
        float2        _scale{ 1.0f, 1.0f };  ///< 피부 UV 에서 층 하나가 덮는 크기
        hashed_string _layer{};
        float32       _opacity{ 1.0f };
        float32       _rotationDegrees{ 0.0f };
        uint8         _bMirror{ SW_FALSE }; ///< u = 0.5 에 대칭인 짝도 그린다(양 볼 홍조 · 양팔 문신)
        uint8         _bHidden{ SW_FALSE }; ///< 목록에 두고 그리지 않는다(편집 창 눈 단추)

        static constexpr float32 kMinScale = 0.01f;
        static constexpr float32 kMaxScale = 4.0f;

        static constexpr uint32  kColorSteps  = 255u;
        static constexpr uint32  kFineSteps   = 65535u; ///< 이동 · 크기 · 회전의 격자(16 비트)
        static constexpr float32 kMaxRotation = 180.0f;

        /** @brief 전송 정밀도로 맞춥니다(색 · 불투명도 8 비트, 이동 · 크기 · 회전 16 비트). 비트 쓰기 · 읽기(`AppearanceSelection`)가 같은 격자를 쓴다. */
        void quantize();
        /** @brief 이동(0..1) · 크기([kMinScale, kMaxScale]) · 회전([−180, 180])을 16 비트 격자 번호로 바꿉니다. */
        static uint32  encodeFine( float32 value, float32 minValue, float32 maxValue );
        static float32 decodeFine( uint32 step, float32 minValue, float32 maxValue );
        bool           operator==( const SkinLayerValue& other ) const;
        /**
         * @brief 피부 UV @p skinUV 가 이 층의 층 UV 어디에 놓이는지 구합니다(굽기 셰이더와 같은 식). 층 UV 가 [0,1]² 밖이면 이 층이 덮지 않는 자리다.
         * @details 층 UV = R(−θ) · (피부 UV − 이동) ÷ 크기 + 0.5. 대칭 짝은 피부 UV 의 u 를 1 − u 로 뒤집어 같은 식을 쓴다(층 UV 의 u 도 뒤집어 거울상).
         */
        float2 toLayerUV( const float2& skinUV, bool bMirrorCopy ) const;
        /**
         * @brief 프리셋의 `<SkinLayer layer color opacity offset scale rotation mirror hidden/>` 하나를 읽습니다(NPC 화장 · 문신). 적지 않은 칸은 이 형식의 초기값이다.
         * @return 층 이름이 없으면 false 이고 오류는 @p report 에 있다.
         */
        [[nodiscard]] bool readFromXML( const XMLNode& node, AppearanceLoadReport& report, string_view sourceName );
        /** @brief `readFromXML` 이 같은 값으로 다시 읽는 원소로 씁니다. */
        void writeToXML( const XMLNode& node ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkinLayerSmearState
     * @brief 층 하나의 번짐 상태입니다(실행 상태 — 공유 코드에 없다). 피부 UV 격자마다 색을 끈 거리(UV 이동)와 묽어진 정도(0..1)를 듭니다.
     * @details 합칠 때 층을 (UV − 이동)에서 읽고 불투명도에 (1 − 묽음)을 곱한다 — 원래 층이 옮겨지고 흐려진다(가짜 데칼이 아니다).
     *          세기에는 늘 (1 − 저항)을 곱한다. 격자는 굽기 텍스처보다 성기다(번짐은 부드럽다) — 굽기가 이웃을 보간해 읽는다.
     */
    class SW_GF_API SkinLayerSmearState
    {
    public:
        SkinLayerSmearState();

        /** @brief 해상도 × 해상도 격자를 비웁니다. */
        void initialize( uint16 resolution );
        /** @brief 모두 지웁니다(씻기 · 되돌리기). */
        void clear();
        /**
         * @brief 흐름(눈물 · 땀 · 비)이 점 @p uv 에서 방향 @p direction 으로 색을 나르고 묽게 합니다. 반지름 @p radius 안의 칸만이다.
         * @param amount 이번 흐름의 세기(0..)입니다. 저항이 곱해진다. 이동은 방향 × 세기 × 반지름, 묽음은 세기 × 묽힘 비율만큼 오른다.
         */
        void advect( const float2& uv, const float2& direction, float32 radius, float32 amount, float32 resistance, float32 dilution );
        /** @brief 접촉(닦기 · 맞음 · 먹기)이 점 @p uv 에서 방향 @p direction 으로 색을 끕니다. 묽음은 오르지 않는다. */
        void drag( const float2& uv, const float2& direction, float32 radius, float32 amount, float32 resistance );
        /** @brief UV 의 이동(가장 가까운 칸)입니다. */
        float2 sampleOffset( const float2& uv ) const;
        /** @brief UV 의 묽음(0..1)입니다. */
        float32 sampleDilution( const float2& uv ) const;
        /** @brief 칸이 바뀔 때마다 오르는 번호입니다(굽기를 다시 할지 가른다). */
        uint32 getRevision() const { return _revision; }
        uint16 getResolution() const { return _resolution; }
        bool   isClean() const { return _bDirty == SW_FALSE; }

    private:
        /** @brief 칸 번호입니다. */
        uint32 findCell( const float2& uv ) const;
        void   apply( const float2& uv, const float2& displacement, float32 radius, float32 dilution );

    private:
        vector<float2>  _listOffset;
        vector<float32> _listDilution;
        uint32          _revision;
        uint16          _resolution;
        uint8           _bDirty;
    };
} // namespace sw

namespace sw
{
    /** @brief 층 하나를 합칠 때 쓰는 값입니다(종류 + 고른 값 + 번짐 — 굽기 셰이더의 층 상수와 같은 모양). */
    struct SkinLayerSample
    {
        float4         _color{};       ///< 층 색(rgb) — 마스크를 곱하기 전
        float32        _coverage{ 0 }; ///< 마스크 × 불투명도 × (1 − 묽음)
        float32        _roughness{ 0.5f };
        float32        _roughnessWeight{ 0.0f };
        float32        _metallic{ 0.0f };
        float32        _metallicWeight{ 0.0f };
        SkinLayerBlend _blend{ SkinLayerBlend::Normal };
    };

    /** @brief 피부 한 픽셀의 표면 값(알베도 · 거칠기 · 금속)입니다. 층 합치기의 CPU 기준 구현을 든다(`skin_layer_composite.hlsli` 와 같은 식 — 테스트 · 섬네일). */
    struct SW_GF_API SkinSurfaceSample
    {
        float3  _albedo{};
        float32 _roughness{ 0.5f };
        float32 _metallic{ 0.0f };

        /** @brief 섞기 한 번입니다. 결과 = lerp(아래, 섞은 색, 덮임). */
        static float3 blend( const float3& below, const float3& layer, float32 coverage, SkinLayerBlend mode );
        /** @brief 이 값 위에 층들을 차례로(앞이 아래) 합친 값입니다. */
        SkinSurfaceSample composite( span<const SkinLayerSample> listLayer ) const;
    };
} // namespace sw

namespace sw
{
    /** @class SkinLayerCatalog @brief `<SkinLayerCatalog><SkinLayer …/></SkinLayerCatalog>`(`skinlayers.xml`)를 읽습니다. */
    class SW_GF_API SkinLayerCatalog
    {
    public:
        [[nodiscard]] bool loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listLayer.clear(); }

        const SkinLayerDef*         findLayer( const hashed_string& id ) const;
        const vector<SkinLayerDef>& getLayers() const { return _listLayer; }

    private:
        vector<SkinLayerDef> _listLayer{};
    };
} // namespace sw
