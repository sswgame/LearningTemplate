/**
 * @file DeferredImageUnloadQueue.h
 * @brief 핫 리로드로 교체된 옛 모듈 이미지를 바로 언로드하지 않고, 최근 몇 번의 연쇄 리로드(배치)만큼 남겨 두는 큐입니다.
 *
 * @note 핫 리로드(`LiveReloadManager`)만 쓰고, Shipping 빌드에서 함께 빠집니다(`Source/ModuleHost/CMakeLists.txt` 의 제외 목록).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @brief 교체된 옛 이미지를 배치 단위로 남겨 두었다가, 배치가 상한을 넘으면 가장 오래된 배치부터 언로드합니다.
     * @details 옛 코드를 가리키는 것(델리게이트, 함수 포인터, vtable, 문자열 리터럴)이 어딘가 남아 있어도, 이미지가 로드돼 있는 동안은
     *          크래시가 나지 않고 옛 동작이 한 번 더 돈다. 배치 안에서는 나중에 넣은 것(의존하는 쪽)부터 언로드합니다.
     *          다른 코드가 구독 중인 채널을 만든 이미지(`bKeepMapped`)는 큐에서만 빼고 언로드하지도, 파일을 지우지도 않습니다.
     */
    class DeferredImageUnloadQueue final
    {
    public:
        /** @param maxBatchCount 남겨 둘 배치 수입니다. 이보다 오래된 배치는 언로드합니다. */
        explicit DeferredImageUnloadQueue( uint32 maxBatchCount );

        /** @brief 새 연쇄 리로드를 시작합니다. 이 뒤에 넣는 이미지는 새 배치에 듭니다. */
        void beginBatch();
        /**
         * @brief 교체된 옛 이미지를 지금 배치에 넣고, 배치가 상한을 넘으면 가장 오래된 배치를 언로드합니다. @p pHandle 이 nullptr 이면 아무것도 하지 않습니다.
         * @param shadowPath 이미지의 섀도 복사본 경로입니다. 언로드할 때 지웁니다.
         * @param bKeepMapped 다른 코드가 아직 구독하는 이벤트 채널을 만든 이미지다(`ModuleImageUtil::releaseModuleCode`) — 언로드하지 않습니다.
         */
        void deferUnload( string_view moduleName, void* pHandle, string_view shadowPath, bool bKeepMapped );
        /**
         * @brief 가장 오래된 배치 하나를 언로드합니다. 배치 안에서는 나중에 넣은 것(의존하는 쪽)부터 언로드합니다.
         * @details 언로드를 미룬 이미지는 **같은 배치의 미룬 이미지나 지금 살아 있는 이미지에만** 바인딩돼 있다(의존이 바뀌면 의존하는
         *          모듈도 같은 연쇄로 바뀐다). 그래서 오래된 배치부터 언로드해도 아직 쓰이는 이미지를 먼저 언로드하는 일이 없다.
         */
        void unloadOldestBatch();
        /** @brief 남은 배치를 오래된 것부터 모두 언로드합니다. 살아 있는 모듈을 언로드하기 **전에** 부릅니다. */
        void unloadAll();

        /** @brief 아직 언로드하지 않은 옛 이미지 수입니다. */
        uint32 getImageCount() const { return static_cast<uint32>( _listImage.size() ); }

    private:
        /** @brief 교체되어 내려갈 차례를 기다리는 옛 이미지입니다. */
        struct DeferredImage
        {
            string _moduleName;
            string _shadowPath;
            void*  _pHandle{ nullptr };
            uint32 _batchID{ 0 };
            bool   _bKeepMapped{ false }; ///< 다른 코드가 아직 구독하는 이벤트 채널을 만든 이미지 — 내리지 않는다(`ModuleImageUtil::releaseModuleCode`)
        };

        vector<DeferredImage> _listImage;     ///< 언로드를 미룬 옛 이미지. 오래된 것부터
        uint32                _batchID;       ///< 연쇄 리로드마다 오르는 배치 번호
        uint32                _maxBatchCount; ///< 남겨 둘 배치 수
    };
} // namespace sw
