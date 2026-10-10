/**
 * @file EditorThumbnailCache.h
 * @brief 콘텐츠 브라우저 썸네일로 쓸 텍스처를 ImGui 텍스처 id 로 들고 있는 작은 LRU 캐시입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorThumbnailCache
     * @brief 텍스처 에셋을 엔진 텍스처 캐시(`TextureCache::acquire`)로 빌려 ImGui 에 등록하고, 최근에 그린 것만 남깁니다(언리얼 썸네일 풀과 같다).
     * @details `findTexture` 는 이번 프레임에 그릴 id 를 돌려주고, 없으면 읽기를 예약합니다. 실제 읽기는 `update` 가 프레임마다 하나씩 합니다.
     *          DDS 를 읽고 GPU 에 올리는 일이 UI 스레드에서 돌기 때문에, 폴더를 열 때 수십 장을 한 프레임에 읽어 멈추지 않게 나눕니다.
     *          해제는 ImGui 등록을 먼저 풀고(`unregisterTexture` 는 그린 스냅샷이 끝난 뒤 디스크립터를 놓는다) 텍스처 캐시의 참조를 놓습니다.
     *          모든 호출은 UI 스레드에서 합니다.
     */
    class EditorThumbnailCache
    {
    public:
        /** @brief 남겨 두는 최대 썸네일 수입니다. 넘으면 가장 오래 그리지 않은 것부터 놓습니다. */
        static constexpr uint32 kCapacity = 64;

        EditorThumbnailCache();
        ~EditorThumbnailCache();
        EditorThumbnailCache( const EditorThumbnailCache& )            = delete;
        EditorThumbnailCache& operator=( const EditorThumbnailCache& ) = delete;

        /** @brief 예약한 읽기 하나를 처리하고 넘친 썸네일을 놓습니다. 프레임마다 그리기 전에 한 번 부릅니다. */
        void update();
        /** @brief 들고 있는 썸네일을 모두 놓습니다(패널 종료, 디바이스 교체). */
        void clear();

        /**
         * @brief @p resourceID 텍스처의 ImGui 텍스처 id 입니다. 아직 없으면 읽기를 예약하고 nullptr 을 돌려줍니다.
         * @details 읽지 못한 경로는 기억해 두고 다시 읽지 않습니다(`clear` 가 잊는다).
         */
        void* findTexture( string_view resourceID );
        /** @brief 지금 들고 있는 썸네일 수입니다. */
        uint32 getCachedCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        /** @brief 썸네일 하나입니다. */
        struct Entry
        {
            string _resourceID;
            void*  _pTextureID{ nullptr };
            uint64 _textureHandle{ 0 }; ///< 등록한 RHI 텍스처 — 바뀌면(다시 읽기, 디바이스 교체) 다시 등록한다
            uint64 _lastUsedFrame{ 0 };
        };

        /** @brief 썸네일 하나의 ImGui 등록과 텍스처 참조를 놓습니다. */
        static void releaseEntry( Entry& entry, IRHIDevice* pDevice );
        /** @brief 예약한 경로 하나를 읽어 등록합니다. 실패하면 실패 목록에 둡니다. */
        void loadPending( IRHIDevice* pDevice );
        /** @brief 이번 프레임에 쓰지 않은 것 가운데 오래된 것부터 용량까지 놓습니다. */
        void evictOverCapacity( IRHIDevice* pDevice );

    private:
        vector<Entry>  _listEntry;
        vector<string> _listPendingID;
        vector<string> _listFailedID;
        uint64         _frame;
    };
} // namespace sw::editor
