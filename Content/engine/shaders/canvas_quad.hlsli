/**
 * canvas_quad.hlsli — 캔버스 사각형 원소 하나(CanvasQuad)의 정의. 그리기(canvas.hlsl)와 화면 마커 투영 컴퓨트(canvas_marker.hlsl)가 이 파일 하나를 쓴다.
 *
 * C++ `CanvasQuad`(Engine/Graphics/Canvas/CanvasDrawList.h)와 바이트 배치가 같아야 한다(CanvasDrawListTest.QuadLayoutMatchesShader). 컴퓨트는 구조 버퍼의 같은 원소 중
 * 맨 앞 앵커 원소의 `_rect` 만 쓴다 — 구조체를 셰이더마다 베끼면 칸을 더할 때 한 곳만 고쳐 스트라이드가 어긋난다(batch_data.hlsli 와 같은 이유).
 */

#ifndef SW_ENGINE_CANVASQUAD_HLSLI
#define SW_ENGINE_CANVASQUAD_HLSLI

struct CanvasQuad
{
    float4 _rect;         // x, y(변환 전 왼쪽 위의 화면 위치) · 너비, 높이(픽셀). 앵커 원소는 (x, y, 배율, -) — C++ CanvasAnchor
    float4 _uvRect;       // u0, v0, u1, v1
    float4 _color;        // 곧은 RGBA
    float4 _borderColor;  // Rect: 테두리 색 · Glyph: 외곽선 색
    float4 _cornerRadius; // 왼위 · 오위 · 오아 · 왼아(픽셀)
    float4 _clipRect;     // 둥근 자르기 x0, y0, x1, y1(픽셀)
    float4 _axis;         // 2×2 변환 — 축 X = xy · 축 Y = zw
    float4 _params;       // Rect: x 테두리 · Glyph: x 거리 배율 · y 외곽선 · z 굵게 · Shadow: y 흐림 · w 둥근 자르기 반지름
    uint   _kind;
    uint   _textureSlot; // 일괄 텍스처 번호(0..3), 없음 = kInvalidIndex
    uint   _flags;
    uint   _anchor; // 화면 앵커 번호, 없음 = kInvalidIndex — 앵커는 버퍼 맨 앞 원소들이고 rect 가 (x, y, 배율, -) 다(C++ CanvasAnchor)
};

#endif // SW_ENGINE_CANVASQUAD_HLSLI
