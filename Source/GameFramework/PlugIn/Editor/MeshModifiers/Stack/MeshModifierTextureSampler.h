/**
 * @file MeshModifierTextureSampler.h
 * @brief 모디파이어가 정점마다 텍스처를 읽도록 이미지 파일을 CPU 로 디코드해 캐시하고 이중선형으로 표본합니다.
 * @details Displace · Displace Render Target · Displace Web Cam 처럼 텍스처를 쓰는 모디파이어의 `IMeshModifierInputs::trySampleTexture` 뒤에 있습니다.
 *          디코드는 Editor 의 `ImageUtil`(stb_image)에 맡깁니다. EngineTest 는 EditorModule 을 링크하지 않아 `ImageUtil.cpp` 를 직접 컴파일합니다.
 *          픽셀은 텍셀마다 float4 로 보관해 HDR(.hdr) 도 클램프 없이 그대로 읽습니다. 8 비트 원본은 0..1 로 나눕니다.
 *          텍셀당 16 바이트이므로 4096² 한 장이 256 MB 입니다. 캐시 합이 512 MB 를 넘으면 1 초 넘게 안 쓴 이미지를 오래된 것부터 내립니다
 *          (지금 적용이 번갈아 쓰는 이미지는 남긴다 — 정점마다 다시 디코드하지 않게). 8 비트 원본을 8 비트로 보관하면 4 배 줄지만 아직 하지 않았습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
    /**
     * @class MeshModifierTextureSampler
     * @brief 경로 키로 디코드한 이미지를 캐시하고 UV 로 표본합니다. 캐시는 인스턴스가 소유합니다(`MeshModifierSession` 이 하나를 들고, 에디터 범위와 함께 사라진다).
     *        여러 작업 스레드에서 동시에 불러도 안전합니다.
     */
    class MeshModifierTextureSampler
    {
    public:
        MeshModifierTextureSampler();
        ~MeshModifierTextureSampler();
        MeshModifierTextureSampler( const MeshModifierTextureSampler& )            = delete;
        MeshModifierTextureSampler& operator=( const MeshModifierTextureSampler& ) = delete;

        /**
         * @brief 이미지 파일을 UV 로 표본합니다.
         * @param assetPath 리소스 id 또는 작업 폴더 상대 경로(이미지 파일). 절대 경로도 받습니다.
         * @param uv [0,1] 범위 밖은 반복(Repeat)으로 감깁니다. 텍셀 중심은 (i + 0.5) / 너비 입니다.
         * @param outRGBA 이중선형 보간한 색입니다. 8 비트 원본은 0..1, HDR 원본은 디코드한 값 그대로입니다.
         * @return 경로가 비었거나 파일을 못 찾거나 디코드에 실패하면 false 이고 outRGBA 는 건드리지 않습니다.
         * @note 파일 수정 시각은 경로마다 일정 간격으로만 다시 봅니다. 바뀌었으면 다시 디코드합니다.
         */
        [[nodiscard]] bool trySample( string_view assetPath, float2 uv, float4& outRGBA ) const;

        /** @brief 캐시를 모두 비웁니다(다음 표본에서 다시 디코드합니다). */
        void clearCache();

    private:
        /** @brief 캐시와 그 잠금입니다(정의는 .cpp). 표본은 캐시를 채우지만 표본기의 겉 상태는 바꾸지 않으므로 `trySample` 은 const 입니다. */
        struct State;
        unique_ptr<State> _pState;
    };
} // namespace sw::editor
