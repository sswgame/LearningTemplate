#include "pch.h"

#include "Engine/Common/RenderTargetDeclaration.h"

#include "Core/Concurrency/atomic.h"

namespace sw
{
    namespace
    {
        /** @brief 묶인 처리 함수입니다. 텍스처 캐시 생성(게임 스레드 · 로더 스레드)과 카메라 등록이 서로 다른 스레드일 수 있어 원자적으로 둔다. */
        atomic<engine::RenderTargetDeclareFunction> s_declareFunction{ nullptr };
    } // namespace

    namespace engine
    {
        void bindRenderTargetDeclaration( RenderTargetDeclareFunction function )
        {
            s_declareFunction.store( function, std::memory_order_release );
        }

        void declareRenderTarget( string_view relativePath, uint32 width, uint32 height )
        {
            const RenderTargetDeclareFunction function = s_declareFunction.load( std::memory_order_acquire );
            if ( function != nullptr )
                function( relativePath, width, height );
        }
    } // namespace engine
} // namespace sw
