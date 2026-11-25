/**
 * @file PathTextureSet.h
 * @brief 렌더 스레드가 경로로 빌린 텍스처 묶음입니다 — 경로마다 `TextureCache` 에 비동기 로드를 청해 핸들을 쥐고, 다 읽힌 것만 내줍니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    class DataPath;
    class Texture2D;

    /**
     * @class PathTextureSet
     * @brief 경로 → 텍스처 로드 핸들 목록입니다(렌더 스레드). 렌더 스레드는 디스크를 기다리지 않는다 — 읽는 동안 그 그리기는 빠진다(하늘 HDRI · 2D 빛 쿠키와 같은 규칙).
     * @details 텍스처는 `TextureCache` 가 소유하고 핸들이 참조를 쥔다(놓으면 캐시에 돌려준다). 못 읽은 경로도 실패한 핸들을 쥐어 프레임마다 다시 읽지 않는다.
     *          쓰는 곳: 후처리 값의 텍스처(컬러 그레이딩 LUT · 블룸 먼지 마스크) · 데칼 · 렌즈 플레어(`FrameRenderState::_pathTextures`), 캔버스의 경로 이미지(`CanvasTextureLeases`),
     *          입자 텍스처(`VFXParticleRenderer`). 몇 개뿐이라 선형 검색이다. 언리얼 Slate 의 브러시도 텍스처가 스트리밍되는 동안 비어 있다.
     *          청하기(`request` · `requestReady`)는 기록 전에만 한다 — 텍스처 생성과 bindless 등록은 기록 중에 할 수 없다. 기록 중에는 `findReady` · `findReadySRV` 만 부른다.
     */
    class SW_API PathTextureSet
    {
    public:
        PathTextureSet();
        ~PathTextureSet();
        PathTextureSet( const PathTextureSet& )            = delete;
        PathTextureSet& operator=( const PathTextureSet& ) = delete;

        /** @brief 처음 보는 @p path 면 로드를 청하고(워커가 읽는다), 이번 프레임에 쓰였다고 표시합니다. 엔진 서비스가 없으면(테스트 하네스 밖) 아무것도 하지 않습니다. */
        void request( const hashed_string& path );
        /**
         * @brief `request` 한 뒤 읽혀 있으면 그 텍스처를 돌려줍니다(읽는 중 · 못 읽음이면 nullptr). 비어 있는 경로는 청하지 않고 nullptr 입니다.
         * @details 못 읽은 경로는 오류를 한 번 남긴다.
         * @param pKind 오류 글에 적는 쓰임(예: "colour grading LUT")
         */
        const Texture2D* requestReady( const DataPath& path, const utf8* pKind );
        /** @brief 청해 둔 @p path 의 읽힌 텍스처입니다. 청하지 않았거나 읽는 중이거나 못 읽었으면 nullptr 입니다(상태를 바꾸지 않는다 — 기록 중에 부른다). */
        const Texture2D* findReady( const hashed_string& path ) const;
        /** @brief `findReady` 의 텍스처가 GPU 자원까지 갖췄으면 그 SRV, 아니면 kInvalidDescriptorIndex 입니다. */
        RHITextureSRVIndex findReadySRV( const hashed_string& path ) const;
        /** @brief 청해 둔 @p path 를 아직 읽는 중이면 true 입니다(못 읽었거나 청하지 않았으면 false — 기다려도 오지 않는다). */
        bool isPending( const hashed_string& path ) const;
        /** @brief 지난 `pruneUnused` 뒤로 청하지 않은 경로의 핸들을 놓고 쓰임 표시를 지웁니다(다음에 다시 쓰이면 다시 읽는다). 부르지 않는 주인은 `release` 까지 모두 쥔다. */
        void pruneUnused();
        /** @brief 모든 핸들을 놓습니다(디바이스 교체 · 종료 — 다음 프레임에 새 디바이스로 다시 청한다). */
        void release();

    private:
        struct Entry
        {
            hashed_string _path{};
            TextureHandle _load{}; ///< 실패해도 쥔다(캐시가 이유를 남겼으므로 다시 묻지 않는다)
            uint8         _bUsed{ SW_FALSE };
            uint8         _bFailureReported{ SW_FALSE }; ///< 못 읽은 경로의 오류를 한 번만 남긴다(`requestReady`)
        };

        /** @brief @p path 의 항목을 찾고, 없으면 로드를 청해 더합니다. 쓰임 표시를 켭니다. */
        Entry&       getOrCreateEntry( const hashed_string& path );
        const Entry* findEntry( const hashed_string& path ) const;

    private:
        vector<Entry> _listEntry;
    };
} // namespace sw
