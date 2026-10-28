/**
 * @file RHIRenderPassLedger.h
 * @brief 렌더 패스 핸들 장부입니다. DX11 · DX12 · GL 이 같이 씁니다(Vulkan 은 `VulkanRHIRenderPassCache` 가 핸들을 쥔다).
 * @details 핸들은 등록 순서의 1 부터 시작하는 번호이고 다시 쓰지 않는다(0 은 백버퍼 패스). 이 백엔드들의 렌더 패스는 서술자를 기록해 두는 것 말고 하는 일이 없어
 *          기록을 읽는 코드가 없다. 그래서 장부는 읽는 길 없이 `add` · `retire` · `clear` 셋뿐이고, 셋이 한 뮤텍스(잎)로 직렬화된다 — 벡터를 락 없이 드러내면 어느 스레드에서
 *          `createRenderPass` 를 불러도 재할당이 다른 호출과 겹칠 수 있다. 기록을 읽어야 하는 일이 생기면 포인터를 내주지 말고 복사본을 돌려주는 읽기를 여기에 더한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class RHIRenderPassLedger
     * @brief 렌더 패스 서술자를 핸들로 보관합니다. 모든 호출을 한 뮤텍스로 직렬화해 어느 스레드에서나 부를 수 있습니다.
     */
    class RHIRenderPassLedger
    {
    public:
        /** @brief 서술자를 등록하고 핸들(1 부터)을 돌려줍니다. */
        [[nodiscard]] RHIRenderPassHandle add( const RHIRenderPassDesc& desc )
        {
            ScopedLock<mutex> lock{ _mutex };
            Record            record{};
            record._desc   = desc;
            record._bAlive = SW_TRUE;
            _listRecord.push_back( record );
            return _listRecord.size();
        }

        /** @brief 핸들의 기록을 죽은 것으로 표시하고 서술자를 비웁니다. 0 이거나 등록한 적 없는 핸들이면 아무 일도 하지 않습니다. */
        void retire( RHIRenderPassHandle handle )
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( handle == 0 || handle > _listRecord.size() )
                return;
            Record& record = _listRecord[handle - 1];
            record._bAlive = SW_FALSE;
            record._desc   = RHIRenderPassDesc{};
        }

        /** @brief 모든 기록을 지웁니다. 장치 종료에서 부릅니다. */
        void clear()
        {
            ScopedLock<mutex> lock{ _mutex };
            _listRecord.clear();
        }

    private:
        struct Record
        {
            RHIRenderPassDesc _desc{};
            uint8             _bAlive{ SW_FALSE };
        };

        mutex                      _mutex;
        vector<Record> _listRecord SW_GUARDED_BY( _mutex );
    };
} // namespace sw
