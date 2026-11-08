/**
 * @file IdentityToken.h
 * @brief 내용 · 객체 한 벌의 정체성 토큰입니다 — 프로세스 번호 발급기(정적 원자 칸) 대신 공유 블록의 주소를 정체성으로 씁니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class IdentityToken
     * @brief 내용 · 객체 한 벌의 정체성입니다. 공유 블록의 주소가 정체성이고, 토큰을 든 쪽이 하나라도 있으면 그 주소는 다른 정체성에 다시 쓰이지 않습니다.
     * @details 내는 쪽(메시 · 리그 · 본 LOD …)은 내용이 바뀔 때 `make` 로 새 토큰을 받는다. 지난 내용을 기억하는 쪽(GPU 풀 · 캐시 · 컴포넌트)은 번호가 아니라
     *          토큰을 값으로 들고 비교한다 — 들고 있으니 주소 재사용(ABA)이 없고, 정적 발급기가 없어 엔진 밖(도구 · 테스트 · 워커)에서 만든 객체도 그대로 쓴다.
     *          원본이 살아 있는 동안만 쓰는 키(한 프레임의 배치 키)는 `getKey` 로 숫자만 써도 된다(원본이 토큰을 들고 있다).
     *          언리얼 `FObjectKey`(전역 객체 표 번호 + 일련번호)와 같은 자리를 전역 표 없이 공유 블록 수명으로 얻는다(`VFXGPURunToken` 과 같은 길).
     */
    class SW_API IdentityToken
    {
    public:
        /** @brief 빈 토큰입니다(정체성 없음 — 예전 번호 0). */
        IdentityToken() = default;

        /** @brief 새 정체성을 만듭니다. 본문은 .cpp 에 둔다 — 공유 블록의 소멸 코드가 엔진 바이너리에 있어 게임 모듈을 다시 올려도 든 쪽이 끊기지 않는다(`Mesh::create` 와 같은 규칙). */
        [[nodiscard]] static IdentityToken make();

        /** @brief 정체성이 있으면 true 입니다. */
        bool isValid() const { return _pBlock != nullptr; }
        /** @brief 정체성의 숫자 키(블록 주소)입니다. 빈 토큰은 0 입니다. 이 토큰(또는 같은 정체성의 사본)이 살아 있는 동안만 고유합니다. */
        uint64 getKey() const { return static_cast<uint64>( reinterpret_cast<uintptr_t>( _pBlock.get() ) ); }
        /** @brief 빈 토큰으로 되돌립니다. */
        void reset() { _pBlock.reset(); }

        /** @brief 해시 키입니다(`DefaultHash` 가 부른다 — `unordered_map<IdentityToken, …>` 이 그대로 쓴다). */
        uint64 computeHash() const { return getKey(); }

        bool operator==( const IdentityToken& other ) const { return _pBlock == other._pBlock; }
        bool operator!=( const IdentityToken& other ) const { return _pBlock != other._pBlock; }

    private:
        /** @brief 정체성 블록입니다(칸은 크기를 0 이 아니게 할 뿐 — 주소가 정체성이다). */
        struct Block
        {
            uint8 _unused{ 0 };
        };

        explicit IdentityToken( shared_ptr<const Block> pBlock )
            : _pBlock{ std::move( pBlock ) }
        {
        }

    private:
        shared_ptr<const Block> _pBlock;
    };
} // namespace sw
