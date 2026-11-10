/**
 * @file RHISwapChainSet.h
 * @brief 메인 창 밖의 창 스왑체인 묶음입니다 — 가진 창 · 이번 프레임에 그린 창 · 다음 프레임에 다시 만들 창을 한 곳에서 지킵니다.
 * @details 다섯 백엔드가 같은 목록 둘(가진 창 + 이 프레임 창)을 따로 들었다. 창을 부술 때 가진 목록에서만 빼면 이 프레임 목록에 매달린 포인터가 남는다 —
 *          `remove` 가 세 목록에서 함께 뺀다. 표시 · 신호 · 다시 만들기 같은 네이티브 일은 백엔드 몫이다.
 *          언리얼 `FRHIViewport` 목록과 Godot `DisplayServer` 창마다 스왑체인을 렌더 장치가 들고 프레임마다 표시할 창을 모으는 것과 같은 자리다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class RHISwapChainSet
     * @brief 백엔드 창 스왑체인(`TSwapChain`) 묶음입니다. 렌더 스레드(와 렌더 스레드를 세운 뒤의 생성 · 파괴)만 쓴다.
     */
    template <typename TSwapChain>
    class RHISwapChainSet
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 가진 창
        // ------------------------------------------------------------------------------
        /** @brief 창 하나를 넘겨받고 그 포인터를 돌려줍니다. */
        TSwapChain* add( unique_ptr<TSwapChain> pSwapChain )
        {
            _listOwned.push_back( std::move( pSwapChain ) );
            return _listOwned.back().get();
        }
        /** @brief 창을 세 목록 모두에서 빼고 놓습니다(이 프레임 · 다시 만들기 목록에 매달리지 않는다). */
        void remove( const TSwapChain& swapChain )
        {
            const auto isThis = [&swapChain]( const TSwapChain* pEntry )
            { return pEntry == &swapChain; };
            (void)VectorUtil::removeIf( _listFrame, isThis );
            (void)VectorUtil::removeIf( _listRebuild, isThis );
            (void)VectorUtil::removeIf( _listOwned, [&swapChain]( const unique_ptr<TSwapChain>& pEntry )
            { return pEntry.get() == &swapChain; } );
        }
        /** @brief 가진 창 목록입니다(장치를 놓거나 GPU 를 비운 뒤 네이티브 객체를 부술 때 돈다). */
        const vector<unique_ptr<TSwapChain>>& getOwnedList() const { return _listOwned; }
        /** @brief 모든 창을 놓습니다(네이티브 객체는 부르는 쪽이 먼저 부쉈다). */
        void clear()
        {
            _listFrame.clear();
            _listRebuild.clear();
            _listOwned.clear();
        }

        // ------------------------------------------------------------------------------
        // 2) 이번 프레임에 그린 창 — endFrame 이 신호 · 표시한 뒤 비운다
        // ------------------------------------------------------------------------------
        /** @brief 이번 프레임에 이 창을 그린다고 적습니다. 처음 적었으면 true 입니다(이미 적혔으면 false). */
        bool markUsedThisFrame( TSwapChain& swapChain )
        {
            if ( isUsedThisFrame( swapChain ) )
                return false;
            _listFrame.push_back( &swapChain );
            return true;
        }
        bool                       isUsedThisFrame( const TSwapChain& swapChain ) const { return std::find( _listFrame.begin(), _listFrame.end(), &swapChain ) != _listFrame.end(); }
        const vector<TSwapChain*>& getFrameList() const { return _listFrame; }
        void                       clearFrame() { _listFrame.clear(); }

        // ------------------------------------------------------------------------------
        // 3) 다음 프레임에 다시 만들 창 — 프레임 중간에 GPU 를 비우지 않는다
        // ------------------------------------------------------------------------------
        void markRebuild( TSwapChain& swapChain )
        {
            if ( std::find( _listRebuild.begin(), _listRebuild.end(), &swapChain ) == _listRebuild.end() )
                _listRebuild.push_back( &swapChain );
        }
        const vector<TSwapChain*>& getRebuildList() const { return _listRebuild; }
        void                       clearRebuild() { _listRebuild.clear(); }

    private:
        vector<unique_ptr<TSwapChain>> _listOwned;
        vector<TSwapChain*>            _listFrame;
        vector<TSwapChain*>            _listRebuild;
    };
} // namespace sw
