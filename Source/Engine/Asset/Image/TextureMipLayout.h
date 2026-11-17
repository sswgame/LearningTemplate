/**
 * @file TextureMipLayout.h
 * @brief DDS 파일 안 밉마다의 바이트 구간(`TextureMipLayout`)입니다 — 머리만으로 구하고, 밉 범위를 파일 구간으로 바꿔 범위 읽기(`IContentFileSource::readRangeAsync`)에 넘긴다.
 * @details 설계: DesignBacklog "텍스처 스트리밍 밉 범위 대기 줄"(MR-1). DDS 는 큰 밉부터 이어지므로 밉 범위가 곧 파일의 한 구간이다(언리얼 `FTexture2DStreamIn_IO` 가 벌크 데이터에서
 *          밉을 오프셋으로 읽는 자리, 유니티 Mipmap Streaming 의 `.resS` 오프셋 읽기). 머리 풀이는 전체 읽기와 같은 `DDSLoader::parseHeader` 다. 파일 없이 테스트한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @struct TextureMipLayout
     * @brief 평면 텍스처(층 하나 · 깊이 1) 한 장의 밉별 파일 구간입니다. 밉 0 이 가장 크다.
     */
    struct SW_API TextureMipLayout
    {
        /** @brief 밉 하나의 파일 구간과 크기입니다. */
        struct MipRange
        {
            uint64 _offset{ 0 }; ///< 파일 처음부터
            uint32 _size{ 0 };
            uint32 _width{ 0 };
            uint32 _height{ 0 };
        };

        /** @brief 머리의 최대 바이트입니다 — 매직 4 + 머리 124 + DX10 머리 20. 이만큼 읽으면 `parse` 에 넉넉하다. */
        static constexpr uint32 kMaxHeaderBytes = 148;

        vector<MipRange> _listMip; ///< 0 = 가장 큰 밉
        uint32           _dxgiFormat{ 0 };

        /**
         * @brief 머리 바이트 @p pHeader(앞 @p headerSize 바이트)와 파일 길이 @p fileSize 로 밉 구간을 구합니다.
         * @details 파일 길이가 머리가 말하는 픽셀 바이트와 다르면(잘린 파일 · 밉 수를 잘못 적은 파일) 오류다. 배열 · 큐브 · 볼륨은 스트리밍하지 않으므로 오류다.
         */
        [[nodiscard]] static expected<TextureMipLayout> parse( const uint8* pHeader, size_t headerSize, uint64 fileSize );
        /** @brief 밉 @p firstMip 부터 @p mipCount 개의 파일 구간입니다(밉은 이어져 있다). 범위를 벗어나면 단언한다. */
        void getRange( uint32 firstMip, uint32 mipCount, uint64& outOffset, uint64& outSize ) const;
    };

    /** @brief 밉 올리기 대기 줄의 원소입니다 — 바이트를 쥘 칸이 없다(읽는 쪽이 꺼낼 때 범위로 읽는다, MR-2). */
    struct TextureMipRequest
    {
        string _key;
        uint32 _firstMip{ 0 };
        uint32 _mipCount{ 0 };
    };
} // namespace sw
