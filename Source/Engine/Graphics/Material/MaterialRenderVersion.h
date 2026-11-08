/**
 * @file MaterialRenderVersion.h
 * @brief 머티리얼 렌더 값(바이트 · stride · 텍스처 슬롯 · 덮어쓰기 · 묶음 값)의 변경 번호입니다. 올릴 때마다 엔진에 묶인 머티리얼 캐시의 렌더 값 세대도 오릅니다.
 * @details 장면 스냅샷은 머티리얼 사본을 매 빌드 다시 묻는 대신, 렌더 값 세대(`MaterialCache::getRenderValueGeneration`)와 머티리얼 밖의 입력(텍스처 다시 올림 · 셰이더 리플렉션 캐시 세대)이
 *          그대로면 사본 맞추기를 통째로 건너뛴다(`GPUMaterialRegistry::refreshMaterialBindings`). 그러려면 값을 바꾸는 모든 곳이 세대를 올려야 한다 —
 *          번호를 올리는 길을 `bump` 하나로 두고 증감 연산자를 두지 않아, 칸의 번호만 올리고 세대를 빠뜨리는 코드를 쓸 수 없게 한다.
 *          세대는 프로세스 정적 칸이 아니라 머티리얼 캐시(엔진 범위)의 칸이다 — 엔진 없이 만든 머티리얼은 읽는 쪽도 "모름" 이라 올릴 곳이 없다.
 *          언리얼은 머티리얼 인스턴스가 바뀌면 렌더 쪽 유니폼 표현을 무효화해 그 프레임에만 다시 모은다(기억에 의존).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class MaterialRenderVersion
     * @brief 칸 하나의 변경 번호이면서, 올릴 때 렌더 값 세대(`MaterialCache::getRenderValueGeneration`)도 함께 올리는 값입니다.
     * @details 칸의 번호는 그 머티리얼을 만지는 스레드의 것이다(예전 `uint32` 칸과 같다). 세대는 캐시의 원자 칸이다(워커에서 올려도 된다).
     */
    class SW_API MaterialRenderVersion
    {
    public:
        MaterialRenderVersion() = default;
        explicit MaterialRenderVersion( uint32 initialValue )
            : _value{ initialValue }
        {
        }

        /** @brief 렌더 값이 바뀌었다 — 이 칸의 번호와 렌더 값 세대를 함께 올립니다. */
        void bump();
        /** @brief 이 칸의 번호입니다(같으면 이 칸의 렌더 값이 그대로다). */
        uint32 get() const { return _value; }

    private:
        uint32 _value{ 0 };
    };

    /**
     * @struct MaterialRenderInputs
     * @brief 머티리얼 사본(`Material::getRenderBinding` · `MaterialInstance::getRenderBinding`)을 정하는 입력 전부의 번호입니다.
     * @details 셋이 모두 같으면 어느 머티리얼도 지난번과 다른 사본을 내지 않는다: 머티리얼 · 인스턴스 · 묶음의 값(`MaterialCache::getRenderValueGeneration`),
     *          다시 올리거나 비동기로 끝난 텍스처의 SRV(`TextureCache::getReloadGeneration`), 셰이더 레이아웃(`ShaderReflectionLibrary::getCacheGeneration`).
     *          사본이 새 입력을 읽는 길을 더하면 여기에도 더한다 — 빠뜨리면 장면 스냅샷이 사본 맞추기를 건너뛰어 그 변화가 화면에 오르지 않는다.
     */
    struct SW_API MaterialRenderInputs
    {
        uint64 _materialRevision{ 0 };
        uint32 _textureReloadGeneration{ 0 };
        uint32 _shaderReflectionGeneration{ 0 };
        uint8  _bKnown{ SW_FALSE }; ///< 텍스처 캐시를 볼 수 없으면(엔진 서비스 없음) 모른다 — 같다고 보지 않는다

        /** @brief 지금 입력을 읽습니다. */
        [[nodiscard]] static MaterialRenderInputs capture();
        /** @brief 둘 다 아는 값이고 모두 같으면 true 입니다. */
        [[nodiscard]] bool isSame( const MaterialRenderInputs& other ) const
        {
            return _bKnown == SW_TRUE && other._bKnown == SW_TRUE && _materialRevision == other._materialRevision &&
                   _textureReloadGeneration == other._textureReloadGeneration && _shaderReflectionGeneration == other._shaderReflectionGeneration;
        }
    };
} // namespace sw
