/**
 * @file FramePostProcessTextureSet.h
 * @brief 뷰의 후처리 값이 가리키는 텍스처(컬러 그레이딩 LUT · 블룸 먼지 마스크)를 경로마다 비동기로 읽어 쥡니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    class DataPath;
    class Texture2D;

    /**
     * @class FramePostProcessTextureSet
     * @brief 후처리 텍스처의 로드 핸들 목록입니다(렌더 스레드). 렌더 스레드는 디스크를 기다리지 않는다 — 읽는 동안 그 효과는 빠진다(하늘 HDRI · 2D 빛 쿠키와 같은 규칙).
     * @details 못 읽은 경로는 오류를 한 번 남기고 실패한 핸들을 쥐어 프레임마다 다시 읽지 않는다. 쓰이지 않은 경로는 `pruneUnused` 가 놓는다(다음 프레임에 다시 쓰이면 다시 읽는다).
     */
    class SW_API FramePostProcessTextureSet
    {
    public:
        FramePostProcessTextureSet();
        ~FramePostProcessTextureSet();
        FramePostProcessTextureSet( const FramePostProcessTextureSet& )            = delete;
        FramePostProcessTextureSet& operator=( const FramePostProcessTextureSet& ) = delete;

        /**
         * @brief @p path 의 텍스처가 읽혀 있으면 돌려줍니다. 처음 보는 경로면 로드를 청하고 nullptr 입니다. 비어 있는 경로는 nullptr 입니다.
         * @param pKind 오류 글에 적는 쓰임(예: "colour grading LUT")
         */
        const Texture2D* findReady( const DataPath& path, const utf8* pKind );
        /** @brief 이번 프레임에 `findReady` 가 묻지 않은 경로의 핸들을 놓고 쓰임 표시를 지웁니다. 프레임에 한 번 부릅니다. */
        void pruneUnused();
        /** @brief 모든 핸들을 놓습니다(디바이스 교체 · 종료 — 다음 프레임에 새 디바이스로 다시 청한다). */
        void release();

    private:
        struct Entry
        {
            hashed_string _path{};
            TextureHandle _load{};
            uint8         _bUsed{ SW_FALSE };
            uint8         _bFailureReported{ SW_FALSE }; ///< 못 읽은 경로의 오류를 한 번만 남긴다
        };

        vector<Entry> _listEntry;
    };
} // namespace sw
