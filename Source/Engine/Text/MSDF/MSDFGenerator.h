/**
 * @file MSDFGenerator.h
 * @brief 글리프 윤곽에서 MSDF(멀티채널 부호 거리장)를 만듭니다 — 모서리가 날카로운 큰 글자용입니다(직접 구현, Chlumsky 의 msdfgen 방식).
 * @details 윤곽을 선분 · 2차 · 3차 베지어 간선으로 받아 모서리에서 갈리게 채널(R · G · B)을 칠하고(`colorEdges`), 텍셀마다 채널별로 그 채널을 가진 간선 중 가장 가까운 것의
 *          부호 있는 의사 거리를 구한다(`computeBitmap`). 세 채널의 중앙값이 글자 안팎을 정하므로 쌍선형 보간해도 모서리가 둥글어지지 않는다. 알파에는 일반 단일 채널 SDF(참 거리)를 함께 둔다.
 *          부호 규약은 FreeType `sdf` 렌더러(`SDFGlyphBitmap`)와 같다 — 128 = 윤곽, 큰 값 = 안쪽, `_rangePx` 만큼 떨어지면 0 · 255.
 *          **윤곽 방향**: 안쪽이 간선 진행 방향의 **왼쪽**(y 위, 반시계 바깥 윤곽)이어야 한다. FreeType 의 TrueType 윤곽(시계 방향)은 부르는 쪽이 뒤집어 넘긴다.
 *          알려진 한계: 겹치는 윤곽(가변 폰트의 글리프)은 겹침을 합치지 않아 겹친 자리에 거짓 선이 생길 수 있다 — 바깥 윤곽끼리 겹치지 않는 폰트에서 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 간선 하나가 속한 채널 비트입니다(셋 모두면 흰색 = 일반 SDF). 모서리에서 만나는 두 간선은 겹치지 않는 채널을 하나씩 더 가져야 한다. */
    namespace MSDFChannel
    {
        inline constexpr uint8 kRed     = 1u;
        inline constexpr uint8 kGreen   = 2u;
        inline constexpr uint8 kBlue    = 4u;
        inline constexpr uint8 kYellow  = kRed | kGreen;
        inline constexpr uint8 kMagenta = kRed | kBlue;
        inline constexpr uint8 kCyan    = kGreen | kBlue;
        inline constexpr uint8 kWhite   = kRed | kGreen | kBlue;
    } // namespace MSDFChannel

    /** @brief 간선 종류입니다. */
    enum class MSDFEdgeKind : uint8
    {
        Linear,    ///< 점 둘
        Quadratic, ///< 점 셋(TrueType 곡선)
        Cubic      ///< 점 넷(PostScript 곡선)
    };

    /** @brief 윤곽의 간선 하나입니다. 점은 시작에서 끝 순서이고, 쓰는 점 수는 종류가 정한다. */
    struct MSDFEdge
    {
        MSDFEdgeKind _kind{ MSDFEdgeKind::Linear };
        uint8        _channels{ MSDFChannel::kWhite }; ///< `MSDFChannel` 비트입니다(`colorEdges` 가 정한다).
        float2       _arrPoint[4]{};
    };
} // namespace sw

namespace sw
{
    /** @brief 닫힌 윤곽 하나입니다. 간선은 이어지고 마지막 끝이 처음 시작과 같다. */
    struct MSDFContour
    {
        vector<MSDFEdge> _listEdge{};
    };
} // namespace sw

namespace sw
{
    /** @brief 글리프 모양입니다(윤곽 여럿). 좌표는 래스터 픽셀이고 y 가 위입니다. */
    struct MSDFShape
    {
        vector<MSDFContour> _listContour{};
    };
} // namespace sw

namespace sw
{
    /** @brief MSDF 비트맵 인자입니다. 텍셀 (열, 행)의 중심은 모양 좌표 (`_origin._x` + 열 + 0.5, `_origin._y` − 행 − 0.5) 입니다. */
    struct MSDFParams
    {
        uint32  _width{ 0 };
        uint32  _height{ 0 };
        float2  _origin{};        ///< 비트맵 왼쪽 위 모서리의 모양 좌표입니다.
        float32 _rangePx{ 6.0f }; ///< 윤곽에서 이 거리(픽셀)까지 거리값을 담는다(`SDFRasterParams::_spreadPx` 와 같은 뜻).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MSDFGenerator
     * @brief MSDF 만들기입니다.
     */
    struct SW_API MSDFGenerator
    {
        /** @brief 간선을 채널로 칠하기 전에 모서리로 보는 각도의 기본값(라디안)입니다(msdfgen 의 3.0). */
        static constexpr float32 kDefaultCornerAngle = 3.0f;

        /**
         * @brief 모서리마다 두 이웃 간선이 겹치지 않는 채널을 갖게 간선을 칠합니다. 모서리 없는 윤곽은 모두 흰색이고, 간선이 모자라면 삼등분합니다.
         * @param cornerAngle 이웃 간선의 방향이 이 각도(라디안)보다 더 꺾이면 모서리입니다(180° 가까이 꺾인 뾰족한 모서리 포함).
         * @param seed        색 고르기의 씨앗입니다(같은 씨앗이면 같은 색 — 결정적).
         */
        static void colorEdges( MSDFShape& inoutShape, float32 cornerAngle = kDefaultCornerAngle, uint64 seed = 0 );
        /**
         * @brief 채널을 칠한 모양에서 `_width` × `_height` RGBA8 비트맵을 만듭니다(행 우선, 빈틈없는 행). RGB 는 MSDF, A 는 일반 SDF 입니다.
         * @details 오차 정정을 한다: 이웃 텍셀 사이를 보간할 때 중앙값이 두 끝의 안팎과 어긋나 거짓 윤곽이 생기는 텍셀 쌍은 그 텍셀을 일반 SDF(세 채널을 중앙값으로)로 접는다.
         */
        static void computeBitmap( const MSDFShape& shape, const MSDFParams& params, vector<uint8>& outBytes );
    };
} // namespace sw
