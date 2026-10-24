/**
 * @file CharacterSurfaceBinding.h
 * @brief 몸 표면 상태(`CharacterSurfaceState` — 젖음 · 흙 · 피 · 찢김)를 부품의 머티리얼 파라미터와 UV 마스크 텍스처로 옮깁니다.
 * @details 상태는 숫자와 마스크일 뿐이라 그리려면 머티리얼에 올라가야 한다. 이 파일이 이름 규칙(머티리얼이 선언해야 읽히는 파라미터 이름)과 올리는 일을 맡는다.
 *          - 채널 스칼라 `Surface_<채널>`: 모든 영역에서 가장 큰 값(캐릭터 전체의 젖음 · 흙 정도).
 *          - 영역 벡터 `SurfaceRegion<영역>_<덩어리>`: 영역의 채널 값을 채널 순서로 네 개씩 묶은 float4(덩어리 0 은 채널 0..3). 영역 번호는 피팅 표의 영역 순서다.
 *          - 마스크 텍스처 `Surface_<채널>_Mask`: 부품 × 채널의 UV 공간 마스크(맞은 위치 스탬프 · 찢김). CPU 텍스처(`TextureCache::createCPUTexture`, 경로 `cpu/surface/…`)이고
 *            값은 R 채널(최댓값을 255 로)이다. 마스크가 바뀌면 같은 텍스처의 픽셀만 갈아 끼운다.
 *          머티리얼이 선언하지 않은 이름은 인스턴스 덮어쓰기로만 남고 그려지는 데는 영향이 없다(염색 파라미터와 같다). 파라미터를 읽어 그리는 머티리얼 셰이더는 따로 둔다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CharacterSurfaceState;
    class MaterialInstance;
    class MeshComponent;
    class SurfaceMask;

    /** @brief 표면 상태가 머티리얼에 올라갈 때 쓰는 이름 규칙입니다(전부 static). 머티리얼 에셋이 같은 이름을 선언한다. */
    struct SW_GF_API SurfaceParameterNames
    {
        /** @brief 영역 벡터 하나에 묶는 채널 수입니다(float4). */
        static constexpr uint32 kChannelsPerVector = 4;

        /** @brief 채널 스칼라(모든 영역의 최댓값) 이름 `Surface_<채널>` 입니다. */
        static hashed_string makeChannel( const hashed_string& channel );
        /** @brief 마스크 텍스처 파라미터 이름 `Surface_<채널>_Mask` 입니다. */
        static hashed_string makeMask( const hashed_string& channel );
        /** @brief 영역 벡터 이름 `SurfaceRegion<영역>_<덩어리>` 입니다. */
        static hashed_string makeRegion( uint32 region, uint32 chunk );
        /** @brief 마스크 CPU 텍스처 경로 `cpu/surface/<소유자>/<부품>/<채널>` 입니다. 소유자 키가 다르면 경로가 겹치지 않는다. */
        static string makeMaskTexturePath( uint64 ownerKey, uint32 part, const hashed_string& channel );
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterSurfaceBinding
     * @brief 표면 상태를 머티리얼 인스턴스에 올리고, 마스크 CPU 텍스처를 쥐고 있다가 놓습니다. 외형 컴포넌트 하나가 하나 가집니다.
     */
    class SW_GF_API CharacterSurfaceBinding
    {
    public:
        CharacterSurfaceBinding() = default;
        /** @brief 쥔 마스크 텍스처를 놓습니다. */
        ~CharacterSurfaceBinding();
        CharacterSurfaceBinding( const CharacterSurfaceBinding& )            = delete;
        CharacterSurfaceBinding& operator=( const CharacterSurfaceBinding& ) = delete;

        /** @brief 메시 컴포넌트의 머티리얼 인스턴스입니다. 없으면 메시의 머티리얼에서 새로 만들어 건다. 머티리얼이 없으면 nullptr 입니다. */
        static MaterialInstance* acquireInstance( MeshComponent& mesh );
        /** @brief 채널 스칼라와 영역 벡터를 인스턴스에 올립니다. */
        static void applyValues( const CharacterSurfaceState& state, MaterialInstance& instance );
        /**
         * @brief 부품의 마스크 텍스처를 만들거나(처음) 갈아 끼우고(마스크가 바뀌었으면) 인스턴스의 마스크 파라미터에 겁니다. 마스크가 있는 채널만 합니다.
         * @param ownerKey 이 캐릭터의 키(오브젝트 번호 같은, 캐릭터마다 다른 값)입니다. 텍스처 경로가 겹치지 않게 한다.
         */
        void applyMasks( uint64 ownerKey, const CharacterSurfaceState& state, uint32 part, MaterialInstance& instance );
        /** @brief 마스크 값을 R 채널(최댓값 → 255)로 둔 RGBA8 픽셀을 만듭니다(비우고 채움). */
        static void makeMaskPixels( const SurfaceMask& mask, float32 maxValue, vector<uint8>& outListPixel );
        /** @brief 쥔 마스크 텍스처를 모두 놓습니다. */
        void releaseTextures();

    private:
        /** @brief 만들어 둔 마스크 텍스처 하나입니다. */
        struct MaskTexture
        {
            string        _path{};
            hashed_string _channel{};
            uint32        _part{ 0 };
            uint32        _revision{ 0 }; ///< 올린 마스크의 번호(`SurfaceMask::getRevision`)
        };

        vector<MaskTexture> _listMaskTexture;
    };
} // namespace sw
