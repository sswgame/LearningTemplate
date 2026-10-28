/**
 * @file EditorThumbnailCache.h
 * @brief 콘텐츠 브라우저 썸네일로 쓸 텍스처를 ImGui 텍스처 id 로 보관하고 있는 작은 LRU 캐시입니다.
 */
#pragma once
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IRHIDevice;
    class TextureLoadBlock;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorThumbnailCache
     * @brief 텍스처 에셋을 엔진 텍스처 캐시(`TextureCache`)에서 빌려 ImGui 에 등록하고, 최근에 그린 것만 남깁니다(언리얼 썸네일 풀과 같다).
     * @details `findTexture` 는 이번 프레임에 그릴 id 를 돌려주고, 없으면 읽기를 예약합니다. 읽기는 세 단계입니다. 예약(`_listPendingID`) →
     *          읽는 중(`_listLoading`, `TextureCache::request` 의 `Async` 핸들을 쥔다) → 등록(`_listEntry`). 디스크 읽기와 DDS 디코드는 워커에서 하므로
     *          UI 스레드가 막히지 않고, 읽는 동안 `findTexture` 는 nullptr 을 돌려줘 호출한 패널이 종류 아이콘 같은 자리 표시를 그립니다.
     *          GPU 업로드는 UI 스레드가 `update` 에서 프레임마다 몇 개씩 합니다(렌더 스레드의 펌프는 주 시점이 있는 패킷에서만 돌아, 그것에 기대면 시점이 없는 동안
     *          썸네일이 영영 오지 않을 수 있다). 읽기에 실패한 경로는 기억해 두고 다시 읽지 않습니다.
     *          해제는 ImGui 등록을 먼저 풀고(`unregisterTexture` 는 그린 스냅샷이 끝난 뒤 디스크립터를 놓는다) 텍스처 캐시의 참조를 놓습니다.
     *          모든 호출은 UI 스레드에서 합니다.
     */
    class EditorThumbnailCache
    {
    public:
        /** @brief 남겨 두는 최대 썸네일 수입니다. 넘으면 가장 오래 그리지 않은 것부터 놓습니다. */
        static constexpr uint32 kCapacity = 64;
        /** @brief 한 번에 워커에 맡겨 두는 읽기 수의 상한입니다. 폴더를 열어 수백 장이 예약돼도 워커 큐를 채우지 않게, 나머지는 예약에 둡니다. */
        static constexpr uint32 kMaxLoading = 8;
        /** @brief `update` 한 번에 UI 스레드가 GPU 에 올리는 썸네일 수의 상한입니다(디코드는 이미 워커에서 끝났다). */
        static constexpr uint32 kMaxUploadsPerUpdate = 2;

        EditorThumbnailCache();
        ~EditorThumbnailCache();
        EditorThumbnailCache( const EditorThumbnailCache& )            = delete;
        EditorThumbnailCache& operator=( const EditorThumbnailCache& ) = delete;

        /** @brief 예약한 읽기를 워커에 맡기고, 읽기를 마친 것을 올려 등록하고, 넘친 썸네일을 놓습니다. 프레임마다 그리기 전에 한 번 부릅니다. */
        void update();
        /** @brief 보관하고 있는 썸네일을 모두 놓고 읽는 중인 핸들도 놓습니다(패널 종료, 디바이스 교체). 읽는 중이던 로드는 엔진이 끝내고 스스로 참조를 놓습니다. */
        void clear();

        /**
         * @brief @p assetID 텍스처의 ImGui 텍스처 id 입니다. 아직 없으면(예약 · 읽는 중 포함) 읽기를 예약하고 nullptr 을 돌려줍니다.
         * @details 읽지 못한 경로는 기억해 두고 다시 읽지 않습니다(`clear` 가 잊는다).
         */
        void* findTexture( string_view assetID );
        /** @brief 지금 보관하고 있는 썸네일 수입니다. */
        uint32 getCachedCount() const { return static_cast<uint32>( _listEntry.size() ); }
        /** @brief 읽기를 기다리거나 읽는 중인 썸네일이 있으면 true 입니다. 그동안 패널은 연속 프레임을 청한다(워커가 끝내도 창 메시지는 오지 않는다). */
        bool hasPendingLoads() const { return _listPendingID.empty() == false || _listLoading.empty() == false; }

    private:
        /** @brief 썸네일 하나입니다. */
        struct Entry
        {
            string _assetID;
            void*  _pTextureID{ nullptr };
            uint64 _textureHandle{ 0 }; ///< 등록한 RHI 텍스처 — 바뀌면(다시 읽기, 디바이스 교체) 다시 등록한다
            uint64 _lastUsedFrame{ 0 };
        };

        /** @brief 워커에서 읽는 중인 썸네일 하나입니다. 핸들이 로드를 살려 둡니다(블록은 에셋이 준비된 뒤 참조 하나를 쥔다). */
        struct Loading
        {
            string                       _assetID;
            shared_ptr<TextureLoadBlock> _pBlock;
        };

        /** @brief 썸네일 하나의 ImGui 등록과 텍스처 참조를 놓습니다. */
        static void releaseEntry( Entry& entry, IRHIDevice* pDevice );
        /** @brief 예약한 경로를 상한(`kMaxLoading`)까지 워커에 맡깁니다. 요청할 수 없는 경로는 실패 목록에 둡니다. */
        void startLoads( IRHIDevice* pDevice );
        /** @brief 읽는 중인 로드를 보아, 워커가 읽기를 마친 것은 올리고(`kMaxUploadsPerUpdate`) 끝난 것은 등록하거나 실패 목록에 둡니다. */
        void finishLoads( IRHIDevice* pDevice );
        /** @brief 준비된 텍스처 하나를 참조를 쥐고 ImGui 에 등록합니다. 못 쓰는 텍스처면 false 이고 아무것도 쥐지 않습니다. */
        bool adoptReady( const string& assetID, IRHIDevice* pDevice );
        /** @brief 이번 프레임에 쓰지 않은 것 가운데 오래된 것부터 용량까지 놓습니다. */
        void evictOverCapacity( IRHIDevice* pDevice );

        /** @brief 실패 경로를 기억하는 수의 상한입니다. 넘으면 가장 오래된 실패부터 잊어, 폴더를 계속 넘겨 보아도 목록이 끝없이 쌓이지 않습니다(잊힌 경로는 다시 보이면 한 번 더 읽어 본다). */
        static constexpr uint32 kMaxFailed = 2048;

    private:
        /** @brief 실패를 기록하고 상한을 넘은 오래된 실패를 잊습니다. */
        void recordFailure( string assetID );

    private:
        vector<Entry>         _listEntry;
        deque<string>         _listPendingID; ///< 읽기를 기다리는 경로(앞에서 꺼낸다)
        vector<Loading>       _listLoading;
        deque<string>         _listFailedID;  ///< 실패한 경로, 오래된 것이 앞이다(`kMaxFailed` 까지)
        unordered_set<string> _uniqueKnownID; ///< 예약 · 읽는 중 · 실패 경로 모두 — `findTexture` 가 프레임마다 목록을 훑지 않고 한 번에 묻는다
        uint64                _frame;
    };
} // namespace sw::editor
