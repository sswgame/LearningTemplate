/**
 * @file TextureLayerBake.h
 * @brief UV 공간 층 굽기 요청입니다 — 밑 텍스처 위에 층(마스크 × 색, 섞기, 배치, 영역 경계, 번짐 텍스처)을 차례로 그려 렌더 타깃 두 장(알베도 · 표면 덮어쓰기)에 굽습니다.
 * @details 게임 스레드가 요청을 텍스처 캐시에 맡기고(`TextureCache::requestLayerBake`) 렌더 스레드가 프레임 프리패스 리스트에서 층마다 전체 화면 한 번씩 그린다(`FramePassExecutor::drawTextureLayerBakes`).
 *          층마다 아래 결과를 읽어야 해서(오버레이 섞기는 고정 블렌드로 못 한다) 결과 · 작업 타깃 두 벌을 번갈아 쓰고, 마지막 층이 결과 타깃에 떨어지게 순서를 잡는다.
 *          층 값은 루트 상수 한 묶음(`kRootDwordCount`)에 담는다 — 굽기는 층 목록이 바뀔 때만 돌아 버퍼를 따로 두지 않는다.
 *          언리얼 `UKismetRenderingLibrary::DrawMaterialToRenderTarget` · 유니티 `CommandBuffer.Blit` 으로 런타임 데칼을 굽는 자리와 같다(기억에 의존한 비교). 식은 `skin_layer_composite.hlsli` 와 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 층 하나입니다(값 — 경로는 텍스처 캐시의 경로). */
    struct SW_API TextureLayerBakeLayer
    {
        string  _mask{};                          ///< 마스크 텍스처(R = 덮임). 배치 층이면 층 UV, 아니면 밑 UV 로 읽는다
        string  _smear{};                         ///< 번짐 텍스처(RG = 이동, B = 묽음 — `kSmearOffsetRange`). 비면 번짐이 없다
        float4  _color{ 1.0f, 1.0f, 1.0f, 1.0f }; ///< rgb = 색, a = 색 알파 × 불투명도
        float2  _offset{ 0.5f, 0.5f };            ///< 층 가운데(밑 UV)
        float2  _scale{ 1.0f, 1.0f };             ///< 층 하나가 덮는 크기(밑 UV)
        float2  _regionMin{ 0.0f, 0.0f };         ///< 영역 경계 — 밖은 덮지 않는다
        float2  _regionMax{ 1.0f, 1.0f };
        float32 _rotationDegrees{ 0.0f };
        float32 _roughness{ 0.5f };
        float32 _roughnessWeight{ 0.0f };
        float32 _metallic{ 0.0f };
        float32 _metallicWeight{ 0.0f };
        uint8   _blend{ 0 };          ///< 0 보통 · 1 곱하기 · 2 오버레이(`skin_layer_composite.hlsli` 의 kSkinLayerBlend*)
        uint8   _bPlaced{ SW_FALSE }; ///< 배치(이동 · 회전 · 크기)를 쓴다
        uint8   _bMirror{ SW_FALSE }; ///< u = 0.5 에 대칭인 짝도 덮는다
    };

    /**
     * @brief 굽기 하나입니다. 타깃은 모두 렌더 텍스처 경로(`rendertarget/…`)이고 요청한 쪽이 핸들(`TextureCache::request`)로 쥐고 있어야 한다 — 렌더 스레드는 `find` 로만 읽는다.
     * @details 층이 없어도 한 번은 그린다(밑을 결과에 옮긴다). 같은 결과 타깃의 새 요청은 앞 요청을 대체한다.
     */
    struct SW_API TextureLayerBakeRequest
    {
        string                        _base{};          ///< 밑 알베도 텍스처
        string                        _albedoTarget{};  ///< 결과 알베도(rgb)
        string                        _surfaceTarget{}; ///< 결과 표면 덮어쓰기(r 거칠기 · g 거칠기 무게 · b 금속 · a 금속 무게)
        string                        _albedoScratch{}; ///< 번갈아 쓰는 작업 타깃(결과와 같은 크기 · 포맷)
        string                        _surfaceScratch{};
        vector<TextureLayerBakeLayer> _listLayer{};

        /** @brief 그리기 횟수입니다(층이 없으면 밑을 옮기는 한 번). */
        uint32 getDrawCount() const { return _listLayer.empty() ? 1u : static_cast<uint32>( _listLayer.size() ); }
        /** @brief 그리기 @p drawIndex 가 결과 타깃에 쓰는지입니다 — 마지막 그리기가 결과에 떨어지게 뒤에서부터 번갈아 짚는다. */
        bool writesResult( uint32 drawIndex ) const { return ( ( getDrawCount() - 1u - drawIndex ) % 2u ) == 0u; }
    };

    /**
     * @brief 층 하나의 루트 상수 묶음입니다(`layer_bake.hlsl` 의 `SW_ROOT_CONSTANTS` 와 같은 배치 — 테스트가 맞춘다).
     * @details 이동은 [0,1], 크기는 [kMinScale, kMaxScale], 회전은 [−180, 180] 을 16 비트 격자로, 색 · 표면 값은 8 비트로 담는다 — 외형의 전송 정밀도(`SkinLayerValue::quantize`)와 같아
     *          보낸 쪽 · 받은 쪽 굽기가 같다. 텍스처 칸(0..3)은 그리는 쪽이 채운다(백엔드마다 전역 번호 또는 서수).
     */
    struct SW_API TextureLayerBakeConstants
    {
        static constexpr uint32 kRootSourceAlbedo  = 0;
        static constexpr uint32 kRootSourceSurface = 1;
        static constexpr uint32 kRootMask          = 2;
        static constexpr uint32 kRootSmear         = 3;
        static constexpr uint32 kRootColor         = 4; ///< RGBA8(r 가 낮은 바이트)
        static constexpr uint32 kRootOffset        = 5; ///< u · v 16 비트(u 가 낮은 쪽)
        static constexpr uint32 kRootScale         = 6;
        static constexpr uint32 kRootRotationFlags = 7; ///< 낮은 16 비트 회전, 16..17 섞기, 18 배치, 19 대칭
        static constexpr uint32 kRootRegionMin     = 8;
        static constexpr uint32 kRootRegionMax     = 9;
        static constexpr uint32 kRootSurface       = 10; ///< 거칠기 · 거칠기 무게 · 금속 · 금속 무게 8 비트씩
        static constexpr uint32 kRootDwordCount    = 11;
        static constexpr uint32 kTextureCount      = 4; ///< 텍스처 칸(밑 알베도 · 밑 표면 · 마스크 · 번짐)

        static constexpr float32 kMinScale    = 0.01f;
        static constexpr float32 kMaxScale    = 4.0f;
        static constexpr float32 kMaxRotation = 180.0f;
        /** @brief 번짐 텍스처의 이동 범위(UV)입니다 — RG 0..1 이 [−범위, +범위] 다. 셰이더 `kLayerBakeSmearRange` 와 같다. */
        static constexpr float32 kSmearOffsetRange = 0.25f;
        static constexpr uint32  kFlagPlaced       = 1u << 18;
        static constexpr uint32  kFlagMirror       = 1u << 19;
        static constexpr uint32  kBlendShift       = 16;
        static constexpr uint32  kUnorm16Max       = 0xFFFFu; ///< 16 비트 격자의 끝 칸
        static constexpr uint32  kUnorm8Max        = 0xFFu;
        static constexpr uint32  kBlendMask        = 0x3u; ///< 섞기 두 비트

        uint32 _arrDword[kRootDwordCount]{};

        /** @brief 층 값을 담습니다(텍스처 칸은 그대로 둔다). */
        void packLayer( const TextureLayerBakeLayer& layer );
        /** @brief [minValue, maxValue] 의 값을 16 비트 격자로 바꿉니다(범위 밖은 끝에서 자른다). */
        static uint32  encodeUnorm16( float32 value, float32 minValue, float32 maxValue );
        static float32 decodeUnorm16( uint32 step, float32 minValue, float32 maxValue );
        /** @brief 0..1 을 8 비트로 바꿉니다. */
        static uint32 encodeUnorm8( float32 value );
    };
} // namespace sw
