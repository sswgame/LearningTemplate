#include "pch.h"

#include "Engine/Graphics/RenderPass/RenderThread.h"

#include "Core/Concurrency/mutex.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"

namespace sw
{
	SW_GLOBAL_VARIABLE_BOOL( gv_useRenderThread, true, "전용 RenderThread 사용 (false = 게임 스레드 인라인 submit)" );

	RenderThread::RenderThread()
		: _pDevice{ nullptr }
		, _pFrameRenderer{ nullptr }
		, _presentHook{}
		, _thread{}
		, _bRunning{ false }
		, _bStop{ false }
		, _bContextBound{ false }
		, _arrRingBuffer{}
		, _arrFrameAllocators{}
		, _head{ 0 }
		, _tail{ 0 }
	{
	}

	RenderThread::~RenderThread()
	{
		stop();
	}

	bool RenderThread::bind( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer )
	{
		if ( pDevice == nullptr )
			return false;

		if ( _bRunning.load( std::memory_order_relaxed ) )
		{
			SW_LOG_WARNING( "[RenderThread] bind() ignored while worker is running." );
			return false;
		}

		_pDevice		= pDevice;
		_pFrameRenderer = pFrameRenderer;
		_bContextBound	= false;
		SW_LOG_INFO( "[RenderThread] Bound for inline submit (no dedicated worker). Backend=%#", pDevice->getBackendName() );
		return true;
	}

	bool RenderThread::start( IRHIDevice* pDevice, FrameRenderer* pFrameRenderer )
	{
		if ( _bRunning.load( std::memory_order_relaxed ) )
			return true;

		if ( bind( pDevice, pFrameRenderer ) == false )
			return false;

		if ( pDevice != nullptr )
			pDevice->unbindGraphicsContext();

		_bStop	  = false;
		_bRunning = true;
		_thread	  = std::thread( &RenderThread::threadMain, this );
		SW_LOG_INFO( "[RenderThread] Dedicated worker started" );
		return true;
	}

	void RenderThread::stop()
	{
		if ( _bRunning.exchange( false, std::memory_order_acq_rel ) == false )
		{
			_pDevice		= nullptr;
			_pFrameRenderer = nullptr;
			_bContextBound	= false;
			return;
		}
		_bStop.store( true, std::memory_order_release );
		_cvProduce.notify_all();
		_cvConsume.notify_all();
		_cvIdle.notify_all();
		if ( _thread.joinable() )
		{
			if ( std::this_thread::get_id() != _thread.get_id() )
				_thread.join();
		}
		_pDevice		= nullptr;
		_pFrameRenderer = nullptr;
		_bContextBound	= false;
		SW_LOG_INFO( "[RenderThread] Stopped" );
	}

	void RenderThread::submit( RenderFramePacket&& packet )
	{
		if ( _pDevice == nullptr )
		{
			SW_LOG_WARNING( "[RenderThread] submit() with no bound device — packet dropped." );
			return;
		}

		// 런타임에 gv_useRenderThread 상태가 동적으로 변경되었을 때 스레드 모드를 핫스왑합니다.
		const bool bShouldRunWorker = gv_useRenderThread;
		if ( _bRunning.load( std::memory_order_acquire ) != bShouldRunWorker )
		{
			IRHIDevice*	   pSavedDevice		   = _pDevice;
			FrameRenderer* pSavedFrameRenderer = _pFrameRenderer;

			if ( bShouldRunWorker )
			{
				if ( _bContextBound )
				{
					_pDevice->unbindGraphicsContext();
					_bContextBound = false;
				}
				start( pSavedDevice, pSavedFrameRenderer );
			}
			else
			{
				waitIdle();
				stop();
				bind( pSavedDevice, pSavedFrameRenderer );
			}
		}

		if ( _bRunning.load( std::memory_order_acquire ) == false )
		{
			executeInline( packet );
			return;
		}

		uint32 currentHead = _head.load( std::memory_order_relaxed );
		uint32 nextHead	   = ( currentHead + 1 ) % _s_kRingCapacity;

		{
			std::unique_lock<mutex> lock{ _mutex };
			_cvProduce.wait( lock, [this, nextHead]()
			{ return _bStop.load( std::memory_order_relaxed ) || nextHead != _tail.load( std::memory_order_acquire ); } );

			if ( _bStop.load( std::memory_order_relaxed ) )
				return;

			_arrRingBuffer[currentHead] = std::move( packet );
			_head.store( nextHead, std::memory_order_release );
		}
		_cvConsume.notify_one();
	}

	void RenderThread::waitIdle()
	{
		if ( _bRunning.load( std::memory_order_acquire ) == false )
			return;

		if ( std::this_thread::get_id() != _thread.get_id() )
		{
			std::unique_lock<mutex> lock{ _mutex };
			_cvIdle.wait( lock, [this]()
			{ return _bStop.load( std::memory_order_relaxed ) || _tail.load( std::memory_order_acquire ) == _head.load( std::memory_order_acquire ); } );
		}

		if ( _pDevice != nullptr )
			_pDevice->waitIdle();
	}

	void RenderThread::executeInline( RenderFramePacket& packet )
	{
		// Game-thread / no-worker path: own context on this thread for the frame.
		ensureContextOnCurrentThread();
		executePacket( packet );
		_arrFrameAllocators[0].reset();
	}

	void* RenderThread::allocateFrameMemory( size_t size, size_t alignment )
	{
		// Game Thread calls this to allocate data for the _head frame
		uint32 currentHead = _head.load( std::memory_order_relaxed );
		if ( _bRunning.load( std::memory_order_relaxed ) )
		{
			uint32 nextHead = ( currentHead + 1 ) % _s_kRingCapacity;
			if ( nextHead == _tail.load( std::memory_order_acquire ) )
			{
				std::unique_lock<mutex> lock{ _mutex };
				_cvProduce.wait( lock, [this, nextHead]()
				{ return _bStop.load( std::memory_order_relaxed ) || nextHead != _tail.load( std::memory_order_acquire ); } );
			}
		}
		return _arrFrameAllocators[currentHead].allocate( size, alignment );
	}

	void RenderThread::threadMain()
	{
		_bContextBound = false;

		for ( ;; )
		{
			uint32 currentTail = _tail.load( std::memory_order_relaxed );

			{
				std::unique_lock<mutex> lock{ _mutex };
				_cvConsume.wait( lock, [this, currentTail]()
				{ return _bStop.load( std::memory_order_relaxed ) || currentTail != _head.load( std::memory_order_acquire ); } );
			}

			if ( _bStop.load( std::memory_order_relaxed ) && currentTail == _head.load( std::memory_order_acquire ) )
				break;

			RenderFramePacket packet = std::move( _arrRingBuffer[currentTail] );
			executePacket( packet );
			_arrFrameAllocators[currentTail].reset();

			{
				std::scoped_lock<mutex> lock{ _mutex };
				_tail.store( ( currentTail + 1 ) % _s_kRingCapacity, std::memory_order_release );
			}
			_cvProduce.notify_one();
			_cvIdle.notify_all();
		}

		if ( _pDevice != nullptr )
		{
			_pDevice->unbindGraphicsContext();
			_bContextBound = false;
		}

		for ( uint32 ringSlotIndex = 0; ringSlotIndex < _s_kRingCapacity; ++ringSlotIndex )
		{
			_arrFrameAllocators[ringSlotIndex].clear();
		}
	}

	void RenderThread::executePacket( RenderFramePacket& packet )
	{
		if ( _pDevice == nullptr || packet._bValid == 0 )
			return;

		ensureContextOnCurrentThread();

		const bool			bOffscreen = packet._gameRenderTarget != 0;
		IRHICommandContext* pImm	   = _pDevice->getImmediateContext();
		if ( bOffscreen )
		{
			if ( pImm == nullptr )
			{
				SW_LOG_ERROR( "[RenderThread] getImmediateContext() is null; skipping offscreen packet" );
				return;
			}
			pImm->beginOffscreenPass( packet._gameRenderTarget, packet._clearColor );
		}
		else
		{
			if ( _pDevice->getSwapChain() == nullptr )
			{
				SW_LOG_ERROR( "[RenderThread] getSwapChain() is null; skipping packet" );
				return;
			}
			_pDevice->getSwapChain()->beginFrame( packet._clearColor );
		}

		if ( _pFrameRenderer != nullptr && _pFrameRenderer->isReady() )
			_pFrameRenderer->executePacket( _pDevice, packet );

		if ( bOffscreen )
		{
			pImm->endOffscreenPass( packet._gameRenderTarget );
			if ( _pDevice->getSwapChain() != nullptr )
				_pDevice->getSwapChain()->beginFrame( packet._clearColor );
		}

		if ( _presentHook.isBound() )
			_presentHook( *_pDevice, packet );

		if ( _pDevice->getSwapChain() != nullptr )
			_pDevice->getSwapChain()->endFrame( true );

		if ( _postPresentHook.isBound() )
			_postPresentHook( *_pDevice, packet );

		if ( _pDevice->requiresExclusiveContextThread() )
		{
			_pDevice->unbindGraphicsContext();
			_bContextBound = false;
		}
	}

	bool RenderThread::ensureContextOnCurrentThread()
	{
		if ( _pDevice == nullptr )
			return false;
		if ( _bContextBound.load( std::memory_order_relaxed ) )
			return true;

		if ( _pDevice->bindGraphicsContext() == false )
		{
			SW_LOG_ERROR( "[RenderThread] bindGraphicsContext failed on executor thread (%#)",
						  _pDevice->getBackendName() );
			return false;
		}
		_bContextBound = true;
		return true;
	}
} // namespace sw
