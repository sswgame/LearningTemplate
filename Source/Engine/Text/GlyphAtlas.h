/**
 * @file GlyphAtlas.h
 * @brief SDF 글리프 아틀라스입니다 — CPU 바이트 페이지(1 채널) · 스카이라인 패킹 · 새로 쓴 구간 · 오래된 페이지 비우기.
 * @details GPU 를 모릅니다. 렌더러(Graphics/Renderer/Canvas)가 프레임마다 `takeUploads` 의 구간 바이트를 받아 자기 거울 페이지와 텍스처에 반영합니다.
 *          그래서 게임 스레드가 페이지를 고치는 동안 렌더 스레드가 같은 바이트를 읽는 일이 없습니다(패킷에 사본이 실린다).
 *          스카이라인(bottom-left): 페이지의 "윤곽선" 마디 목록에서 사각형 윗변이 가장 낮아지는 자리를 고른다 — 글리프처럼 높이가 비슷한 작은 사각형에 알맞다
 *          (Jukka Jylänki, "A Thousand Ways to Pack the Bin").
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 아틀라스 안의 사각형 하나입니다(페이지 · 텍셀 좌표). */
    struct GlyphAtlasRect
    {
        uint16 _page{ 0 };
        uint16 _x{ 0 };
        uint16 _y{ 0 };
        uint16 _width{ 0 };
        uint16 _height{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 렌더러에 넘기는 업로드 한 건입니다 — 페이지의 한 구간과 그 바이트 사본(빈틈없는 행). */
    struct GlyphAtlasUpload
    {
        vector<uint8> _bytes{};                ///< 구간 바이트 사본(행 우선 · `_width` 바이트 행)
        uint16        _page{ 0 };              ///< 페이지 번호
        uint16        _x{ 0 };                 ///< 구간 왼쪽(텍셀)
        uint16        _y{ 0 };                 ///< 구간 위(텍셀)
        uint16        _width{ 0 };             ///< 구간 너비
        uint16        _height{ 0 };            ///< 구간 높이
        uint8         _bWholePage{ SW_FALSE }; ///< 페이지를 새로 만들었거나 비웠다 — 렌더러는 페이지 텍스처를 (다시) 만들고 전체를 올린다
    };
} // namespace sw

namespace sw
{
    /**
     * @class GlyphAtlas
     * @brief 글리프 SDF 를 담는 페이지 묶음입니다(게임 스레드 전용).
     */
    class SW_API GlyphAtlas
    {
    public:
        static constexpr uint32 kPageSize     = 1024; ///< 페이지 한 변(텍셀). R8 이라 페이지 1 MiB
        static constexpr uint32 kMaxPageCount = 8;    ///< 이 수를 넘으면 오래된 페이지를 비운다
        static constexpr uint32 kPadding      = 1;    ///< 글리프 사이 빈 텍셀(쌍선형 표본이 이웃을 읽지 않게)

        GlyphAtlas();

        /**
         * @brief @p width × @p height 자리를 잡습니다(여백은 이쪽이 더한다). 지금 페이지들에 자리가 없으면 페이지를 늘립니다.
         * @return 최대 페이지까지 다 차 자리가 없으면 false — 부르는 쪽(GlyphCache)이 `evictLeastRecentlyUsedPage` 뒤 다시 부릅니다.
         */
        [[nodiscard]] bool allocate( uint32 width, uint32 height, GlyphAtlasRect& outRect );
        /** @brief 잡은 자리에 바이트를 씁니다(행 우선 · 빈틈없는 행) — 업로드 구간으로 기록합니다. */
        void write( const GlyphAtlasRect& rect, const uint8* pBytes );
        /** @brief 이번 프레임에 그 페이지를 썼다고 적습니다(비우기 순서의 근거). */
        void markPageUsed( uint32 page, uint64 frameIndex );
        /**
         * @brief @p currentFrame 에 쓰이지 않은 페이지 중 가장 오래 안 쓴 것을 비웁니다. 비운 페이지 번호를 돌려주고, 비울 것이 없으면(모두 이번 프레임 사용) false.
         * @details 세대(`getGeneration`)가 오르고 그 페이지 전체 업로드가 기록됩니다.
         */
        [[nodiscard]] bool evictLeastRecentlyUsedPage( uint64 currentFrame, uint32& outPage );

        /** @brief 지난 호출 뒤 바뀐 구간들을 바이트 사본과 함께 @p outListUpload 뒤에 붙이고 비웁니다(게임 스레드가 패킷을 채울 때). */
        void takeUploads( vector<GlyphAtlasUpload>& outListUpload );

        /** @brief 지금 페이지 수입니다. */
        uint32 getPageCount() const { return static_cast<uint32>( _listPage.size() ); }
        /** @brief 페이지 바이트입니다(`kPageSize` × `kPageSize`, 행 우선). */
        const uint8* getPageBytes( uint32 page ) const { return _listPage[page]._bytes.data(); }
        /** @brief 페이지를 비울 때마다 오릅니다. 칠해 둔 글자 사각형이 이 값과 다르면 다시 칠합니다. */
        uint32 getGeneration() const { return _generation; }

    private:
        struct SkylineNode
        {
            uint16 _x{ 0 };
            uint16 _y{ 0 };
            uint16 _width{ 0 };
        };

        struct Page
        {
            vector<uint8>          _bytes{};                    ///< 페이지 바이트(0 = 가장 먼 바깥)
            vector<SkylineNode>    _listSkyline{};              ///< 스카이라인 마디(x 순)
            vector<GlyphAtlasRect> _listDirty{};                ///< 이번 프레임에 쓴 구간(업로드 때 하나로 합치지 않는다)
            uint64                 _lastUsedFrame{ 0 };         ///< 마지막으로 쓴 프레임
            uint8                  _bWholePageDirty{ SW_TRUE }; ///< 페이지 전체를 올려야 한다(새 페이지 · 비운 페이지)
        };

        /** @brief 빈 페이지를 하나 더합니다(스카이라인 = 바닥 한 마디). */
        void addPage();
        /** @brief 페이지 하나에서 자리를 찾고 스카이라인을 고칩니다. */
        [[nodiscard]] static bool allocateInPage( Page& page, uint32 width, uint32 height, uint16& outX, uint16& outY );
        /** @brief 마디 @p nodeIndex 에서 시작해 너비 @p width 가 들어가면 그 자리의 바닥 높이를 줍니다. */
        [[nodiscard]] static bool findFitHeight( const Page& page, size_t nodeIndex, uint32 width, uint32 height, uint32& outY );

        vector<Page> _listPage;
        uint32       _generation;
    };
} // namespace sw
