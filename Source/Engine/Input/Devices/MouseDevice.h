/**
 * @file MouseDevice.h
 * @brief 독립된 표준 마우스 입력 장치 클래스 (버튼, 좌표, 1:1 Raw 델타, 휠, 커서 모드)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
	/** @brief 마우스 커서 잠금 및 클리핑 모드 */
	enum class MouseLockMode : uint8
	{
		None = 0,
		ConfinedToWindow,
		LockedInCenter
	};
	using CursorLockMode = MouseLockMode;

	/**
	 * @class MouseDevice
	 * @brief 마우스 버튼, 좌표, 센서 델타 및 커서 컨텍스트를 전담하는 IInputDevice 구현체
	 */
	class SW_API MouseDevice : public IInputDevice
	{
	public:
		MouseDevice();
		virtual ~MouseDevice() override = default;

		MouseDevice( const MouseDevice& )			 = delete;
		MouseDevice& operator=( const MouseDevice& ) = delete;

		// ------------------------------------------------------------------------------
		// 1) IInputDevice 수명주기
		// ------------------------------------------------------------------------------
		InputDeviceKind getDeviceKind() const override { return InputDeviceKind::Mouse; }
		string_view		getDeviceName() const override { return "Mouse"; }
		bool			isConnected() const override { return true; }

		void poll( float32 deltaTime ) override;
		void onFrameBegin( float32 deltaTime ) override;
		void onFrameEnd() override;
		void resetState() override;

		bool	isControlDown( uint16 controlIndex ) const override;
		bool	wasControlPressed( uint16 controlIndex ) const override;
		bool	wasControlReleased( uint16 controlIndex ) const override;
		float32 getControlValue( uint16 controlIndex ) const override;

		// ------------------------------------------------------------------------------
		// 2) 마우스 전용 쿼리
		// ------------------------------------------------------------------------------
		bool isButtonDown( MouseButton button ) const;
		bool wasButtonPressed( MouseButton button ) const;
		bool wasButtonReleased( MouseButton button ) const;
		bool wasAnyButtonPressed() const { return _pressedMask != 0; }

		void getPosition( int32& outX, int32& outY ) const
		{
			outX = _mouseX;
			outY = _mouseY;
		}
		int32 getPositionX() const { return _mouseX; }
		int32 getPositionY() const { return _mouseY; }
		void  getDelta( int32& outDx, int32& outDy ) const
		{
			outDx = _deltaX;
			outDy = _deltaY;
		}
		void getRawDelta( float32& outDx, float32& outDy ) const
		{
			outDx = _rawDeltaX;
			outDy = _rawDeltaY;
		}
		void getSmoothDelta( float32& outDx, float32& outDy ) const
		{
			outDx = _smoothDeltaX;
			outDy = _smoothDeltaY;
		}
		float32 getSmoothing() const { return _smoothingFactor; }
		void	setSmoothing( float32 factor ) { _smoothingFactor = factor < 0.0f ? 0.0f : ( factor > 0.99f ? 0.99f : factor ); }
		float32 getAcceleration() const { return _accelerationPower; }
		void	setAcceleration( float32 power ) { _accelerationPower = power < 1.0f ? 1.0f : power; }

		float32 getMouseWheel() const { return _mouseWheelDelta; }
		float32 getMouseWheelHorizontal() const { return _mouseWheelHorizontalDelta; }

		bool isPointerInside() const { return _bPointerInside == SW_TRUE; }
		bool wasPointerEntered() const { return _bPointerEntered == SW_TRUE; }
		bool wasPointerLeft() const { return _bPointerLeft == SW_TRUE; }

		MouseLockMode  getLockMode() const { return _lockMode; }
		void		   setLockMode( MouseLockMode mode ) { _lockMode = mode; }
		CursorLockMode getCursorLockMode() const { return _lockMode; }
		void		   setCursorLockMode( CursorLockMode mode ) { _lockMode = mode; }
		bool		   isCursorVisible() const { return _bCursorVisible == SW_TRUE; }
		void		   setCursorVisible( bool bVisible ) { _bCursorVisible = bVisible ? SW_TRUE : SW_FALSE; }

		void setClipSubRect( int32 left, int32 top, int32 right, int32 bottom )
		{
			_clipSubRectLeft   = left;
			_clipSubRectTop	   = top;
			_clipSubRectRight  = right;
			_clipSubRectBottom = bottom;
			_bHasSubRect	   = SW_TRUE;
		}
		void clearClipSubRect()
		{
			_clipSubRectLeft   = 0;
			_clipSubRectTop	   = 0;
			_clipSubRectRight  = 0;
			_clipSubRectBottom = 0;
			_bHasSubRect	   = SW_FALSE;
		}
		bool hasClipSubRect() const { return _bHasSubRect == SW_TRUE; }
		bool getClipSubRect( int32& outLeft, int32& outTop, int32& outRight, int32& outBottom ) const
		{
			outLeft	  = _clipSubRectLeft;
			outTop	  = _clipSubRectTop;
			outRight  = _clipSubRectRight;
			outBottom = _clipSubRectBottom;
			return _bHasSubRect == SW_TRUE;
		}

		// ------------------------------------------------------------------------------
		// 3) OS 이벤트 핸들러
		// ------------------------------------------------------------------------------
		void setButtonDown( MouseButton button, bool bDown );
		void setPosition( int32 x, int32 y );
		void addRawDelta( float32 dx, float32 dy );
		void addWheelDelta( float32 delta );
		void addHorizontalWheelDelta( float32 delta );
		void setPointerInsideState( bool bInside );

	private:
		void updateSmoothDelta( float32 dx, float32 dy );

		static constexpr size_t kButtonCount = static_cast<size_t>( MouseButton::Count );

		int32				   _mouseX;					   /**< 현재 프레임의 마우스 화면 좌표 X (윈도우 클라이언트 기준). */
		int32				   _mouseY;					   /**< 현재 프레임의 마우스 화면 좌표 Y. */
		int32				   _prevMouseX;				   /**< 직전 프레임의 마우스 좌표 X. getDelta() 계산에 사용. */
		int32				   _prevMouseY;				   /**< 직전 프레임의 마우스 좌표 Y. */
		int32				   _deltaX;					   /**< 이번 프레임의 좌표 이동량(_mouseX - _prevMouseX). 화면 경계에 막히면 실제 이동보다 작게 나올 수 있음. */
		int32				   _deltaY;					   /**< 이번 프레임의 좌표 이동량 Y. */
		float32				   _rawDeltaX;				   /**< OS 원시(Raw Input) 델타 누적값 X. 화면 경계 클램핑 없이 실제 이동량을 반영 (FPS 카메라 룩에 적합). */
		float32				   _rawDeltaY;				   /**< OS 원시 델타 누적값 Y. */
		float32				   _smoothDeltaX;			   /**< 감도/가속/스무딩(EMA)이 적용된 최종 델타 X. getSmoothDelta()가 반환하는 값. */
		float32				   _smoothDeltaY;			   /**< 스무딩 적용된 최종 델타 Y. */
		float32				   _smoothingFactor;		   /**< EMA 스무딩 계수 [0.0, 0.99]. 0이면 스무딩 없이 원시 델타를 그대로 사용. */
		float32				   _accelerationPower;		   /**< 마우스 가속 지수. 1.0이면 가속 없음, 클수록 빠르게 움직일 때 델타가 더 커짐. */
		float32				   _accumulatedRawDx;		   /**< 현재 미사용(항상 0으로 리셋만 됨) — 프레임 간 원시 델타 누적용으로 남겨둔 예비 필드. */
		float32				   _accumulatedRawDy;		   /**< 현재 미사용. _accumulatedRawDx와 동일. */
		float32				   _mouseWheelDelta;		   /**< 이번 프레임 수직 휠 회전량. getMouseWheel()이 반환하는 값. */
		float32				   _mouseWheelAccum;		   /**< _mouseWheelDelta와 동일한 값을 갖는 중복 필드(현재 별도로 조회되지 않음). */
		float32				   _mouseWheelHorizontalDelta; /**< 이번 프레임 수평 휠(틸트) 회전량. */
		float32				   _mouseWheelHorizontalAccum; /**< _mouseWheelHorizontalDelta와 동일한 값을 갖는 중복 필드(현재 별도로 조회되지 않음). */
		int32				   _clipSubRectLeft;		   /**< 마우스 클리핑 서브 영역(클라이언트 좌표 기준, setClipSubRect로 설정). */
		int32				   _clipSubRectTop;
		int32				   _clipSubRectRight;
		int32				   _clipSubRectBottom;
		MouseLockMode		   _lockMode;	  /**< 커서 잠금 모드 (None/ConfinedToWindow/LockedInCenter). InputManager::applyMouseLockMode()가 실제 OS ClipCursor를 적용. */
		uint8				   _buttonMask;	  /**< 이번 프레임의 버튼 눌림 비트마스크 (MouseButton 인덱스로 비트 조회). */
		uint8				   _pressedMask;  /**< 이번 프레임에 새로 눌린 버튼 비트마스크 (엣지). onFrameBegin/onFrameEnd에서 초기화. */
		uint8				   _releasedMask; /**< 이번 프레임에 새로 떼어진 버튼 비트마스크 (엣지). */
		uint8				   _bCursorVisible	  : 1;
		uint8				   _bPointerInside	  : 1; /**< 마우스 포인터가 현재 창 클라이언트 영역 안에 있는지. */
		uint8				   _bPointerEntered	  : 1; /**< 이번 프레임에 포인터가 창 안으로 새로 들어왔는지(엣지). */
		uint8				   _bPointerLeft	  : 1; /**< 이번 프레임에 포인터가 창 밖으로 새로 나갔는지(엣지). */
		uint8				   _bAnyButtonPressed : 1; /**< 이번 프레임에 어떤 버튼이든 새로 눌렸는지. wasAnyButtonPressed()가 참조. */
		uint8				   _bHasSubRect		  : 1; /**< _clipSubRectXxx로 지정한 서브 영역 클리핑이 활성화되어 있는지. */
		[[maybe_unused]] uint8 _reserved		  : 2;
	};
} // namespace sw
