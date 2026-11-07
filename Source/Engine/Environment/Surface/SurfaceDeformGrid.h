/**
 * @file SurfaceDeformGrid.h
 * @brief 표면 변형 층의 높이 격자입니다 — 한 변이 `extent` m 인 정사각 창을 `resolution` 칸으로 나눠 칸마다 지면 기준 높이(m)와 메워지는 속도를 듭니다.
 * @details 도장(`SurfaceStampDef`)을 찍으면 꺼지는 칸은 더 깊은 쪽, 솟는 칸은 꺼지지 않은 칸에서만 더 높은 쪽을 남긴다(발자국 위의 다른 발자국 테두리가 자국을
 *          메우지 않는다). 칸은 마지막으로 바꾼 도장의 속도로 0 을 향해 메워진다. 창은 월드에 칸 단위로 붙어 있고, 옮기면 겹치는 칸의 내용을 그대로 민다.
 *          바뀐 칸만 텍스처로 보낸다(`hasDirtyRect` · `writeTexels`) — 자국이 없는 칸은 일하지 않는다.
 *          텍스처 인코딩(RGBA8): R = 1 - 꺼짐 / 범위, G = 1 - 솟음 / 범위, B = A = 1. 흰색이 평평이라, 텍스처가 아직 없거나 흰색으로 읽히는 자리가 땅을 옮기지 않는다
 *          (`surface_deform.hlsl` 이 높이 = (R - G) × 범위 로 푼다). 엔진 · 셰이더 밖에 기대는 것이 없어 테스트가 바로 쓴다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    struct SurfaceStampDef;

    /**
     * @class SurfaceDeformGrid
     * @brief 표면 변형 층의 CPU 높이 격자입니다(파일 머리말 참고).
     */
    class SW_API SurfaceDeformGrid
    {
    public:
        /** @brief 칸 사각형(시작 포함, 끝 제외)입니다. */
        struct TexelRect
        {
            uint32 _minX{ 0 };
            uint32 _minZ{ 0 };
            uint32 _maxX{ 0 };
            uint32 _maxZ{ 0 };

            bool   isEmpty() const { return _maxX <= _minX || _maxZ <= _minZ; }
            uint32 getWidth() const { return _maxX - _minX; }
            uint32 getHeight() const { return _maxZ - _minZ; }
            /** @brief @p other 를 품게 넓힙니다. 빈 사각형은 무시한다. */
            void merge( const TexelRect& other );
        };

        SurfaceDeformGrid();

        /** @brief 격자를 @p resolution 칸 · 한 변 @p extent m 로 비우고 창을 @p centerX · @p centerZ 둘레에 둡니다. 모두 다시 보낼 칸이 된다. */
        void reset( uint32 resolution, float32 extent, float32 centerX, float32 centerZ );
        /** @brief `reset` 을 했는지입니다. */
        bool isReady() const { return _resolution > 0; }

        /**
         * @brief 창의 중심을 @p centerX · @p centerZ 가까이(칸 단위)로 옮깁니다. 겹치는 칸의 내용을 밀고 새로 들어온 칸은 평평하다. 옮겼으면 true 입니다.
         * @details 창이 움직이면 텍스처 전부를 다시 보내므로 부르는 쪽이 문턱을 둔다(`SurfaceDeformSystem` — 창 너비의 일부만큼 벗어났을 때).
         */
        bool recenter( float32 centerX, float32 centerZ );

        /**
         * @brief 도장 @p def 를 월드 (@p centerX, @p centerZ) 에 요 @p yaw(라디안, +Z 앞)로 찍습니다. 창 밖 칸은 버린다.
         * @param length Trail 이 두 점 사이에 찍힐 때의 길이(m, 0 = 도장의 길이)
         * @param phase  Funnel 홈의 회전(라디안)
         */
        void applyStamp( const SurfaceStampDef& def, float32 centerX, float32 centerZ, float32 yaw, float32 length, float32 phase );
        /** @brief 칸마다 0 을 향해 (메워지는 속도 × @p deltaTime) 만큼 메웁니다. 자국이 남은 칸만 훑는다. */
        void fill( float32 deltaTime );

        /** @brief 월드 (@p x, @p z) 가 든 칸의 높이(m, 음수 = 꺼짐)입니다. 창 밖은 0 이다. */
        float32 findHeightAt( float32 x, float32 z ) const;
        /** @brief 높이가 0 이 아닌 칸 수입니다. */
        uint32 countActiveTexels() const;
        /** @brief 가장 깊은 꺼짐(m, 0 이상)입니다. */
        float32 computeMaxDepth() const;
        /** @brief 월드 (@p centerX, @p centerZ) 둘레 한 변 2 × @p halfSize 정사각형 안에서 가장 깊은 꺼짐(m, 0 이상)입니다(창 밖 칸은 0). */
        float32 computeMaxDepthNear( float32 centerX, float32 centerZ, float32 halfSize ) const;

        /** @brief 텍스처로 보낼 칸이 있는지입니다. */
        bool             hasDirtyRect() const { return _dirtyRect.isEmpty() == false; }
        const TexelRect& getDirtyRect() const { return _dirtyRect; }
        /** @brief 보낼 칸을 비웁니다(보낸 뒤). */
        void clearDirtyRect() { _dirtyRect = TexelRect{}; }
        /**
         * @brief @p rect 칸들을 RGBA8 로 씁니다(행은 z, 열은 x — 파일 머리말의 인코딩). @p depthRange(m) 를 넘는 높이는 범위에 붙는다.
         * @param outListByte 크기를 @p rect 에 맞춰 다시 채운다
         */
        void writeTexels( const TexelRect& rect, float32 depthRange, vector<uint8>& outListByte ) const;

        uint32  getResolution() const { return _resolution; }
        float32 getExtent() const { return _extent; }
        float32 getTexelSize() const { return _texelSize; }
        /** @brief 창의 최소 모서리(월드 x, z)입니다. */
        float2 getOrigin() const;

    private:
        /** @brief 월드 x(또는 z)를 월드 칸 번호로 바꿉니다(칸 경계는 텍셀 크기의 배수). */
        int32 toWorldCell( float32 world ) const;
        /** @brief 칸 (@p x, @p z) 의 배열 번호입니다. */
        uint32 toIndex( uint32 x, uint32 z ) const { return z * _resolution + x; }

    private:
        vector<float32> _listHeight;    ///< 칸마다 지면 기준 높이(m)
        vector<float32> _listFillSpeed; ///< 칸마다 마지막으로 바꾼 도장의 메워지는 속도(m/s)
        uint32          _resolution;
        float32         _extent;
        float32         _texelSize;
        int32           _originCellX; ///< 창의 최소 모서리가 든 월드 칸
        int32           _originCellZ;
        TexelRect       _activeRect; ///< 높이가 0 이 아닌 칸을 모두 품는 사각형
        TexelRect       _dirtyRect;  ///< 지난번 보낸 뒤 바뀐 칸
    };
} // namespace sw
