/**
 * @file ZstdDictionaryCompressor.h
 * @brief zstd 학습 사전으로 작은 덩어리(네트워크 패킷)를 압축 · 해제합니다 — 사전과 문맥을 미리 만들어 둡니다.
 * @details 등록부의 코덱이 아니다 — 사전은 양쪽이 같은 것을 가져야 해 코덱 id 하나로 고를 수 없다. 지금은 측정(`NetCompressionBenchTest`)만 쓴다.
 *          한 스레드가 쓴다(문맥을 다시 쓴다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class SW_API ZstdDictionaryCompressor
    {
    public:
        ZstdDictionaryCompressor();
        ~ZstdDictionaryCompressor();

        ZstdDictionaryCompressor( const ZstdDictionaryCompressor& )            = delete;
        ZstdDictionaryCompressor& operator=( const ZstdDictionaryCompressor& ) = delete;

        /** @brief 표본에서 사전을 배웁니다(ZDICT). 표본이 적거나 배울 것이 없으면 false 입니다. */
        [[nodiscard]] static bool trainDictionary( const vector<vector<uint8>>& listSample, int32 dictionaryByteCount, vector<uint8>& outDictionaryBytes );

        /** @brief 사전으로 압축 · 해제 문맥을 만듭니다. */
        [[nodiscard]] bool initialize( const vector<uint8>& dictionaryBytes, int32 level );
        void               shutdown();

        /** @brief @p outBytes 를 비우고 압축 결과를 씁니다. */
        [[nodiscard]] bool compress( const uint8* pSource, int32 sourceSize, vector<uint8>& outBytes );
        /** @brief 풀어서 @p outBytes 에 씁니다. @p maxRawSize 를 넘게 풀리면 false 입니다(압축 폭탄). */
        [[nodiscard]] bool decompress( const uint8* pSource, int32 sourceSize, int32 maxRawSize, vector<uint8>& outBytes );

    private:
        struct State;

        unique_ptr<State> _state;
    };
} // namespace sw
