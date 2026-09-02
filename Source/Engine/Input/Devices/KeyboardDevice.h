/**
 * @file KeyboardDevice.h
 * @brief 독립된 표준 키보드 입력 장치 클래스
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
	/**
	 * @class KeyboardDevice
	 * @brief 키보드 키 상태 및 텍스트/IME 입력을 전담하는 IInputDevice 구현체
	 */
	class SW_API KeyboardDevice : public IInputDevice
	{
	public:
		using TextInputDelegate = Delegate<void( string_view )>;

		KeyboardDevice();
		virtual ~KeyboardDevice() override = default;

		KeyboardDevice( const KeyboardDevice& )			   = delete;
		KeyboardDevice& operator=( const KeyboardDevice& ) = delete;

		// ------------------------------------------------------------------------------
		// 1) IInputDevice 수명주기
		// ------------------------------------------------------------------------------
		InputDeviceKind getDeviceKind() const override { return InputDeviceKind::Keyboard; }
		string_view		getDeviceName() const override { return "Keyboard"; }
		bool			isConnected() const override { return true; }

		void poll( float32 deltaTime ) override;
		void onFrameBegin( float32 deltaTime ) override;
		void onFrameEnd() override;
		void resetState() override;

		bool isControlDown( uint16 controlIndex ) const override;
		bool wasControlPressed( uint16 controlIndex ) const override;
		bool wasControlReleased( uint16 controlIndex ) const override;

		// ------------------------------------------------------------------------------
		// 2) 키보드 전용 쿼리 & OS 이벤트 핸들러
		// ------------------------------------------------------------------------------
		bool isKeyDown( Key key ) const;
		bool wasKeyPressed( Key key ) const;
		bool wasKeyReleased( Key key ) const;
		bool wasAnyKeyPressed() const { return _bAnyKeyPressed == SW_TRUE; }

		void setKeyDown( Key key, bool bDown );
		void notifyTextInput( string_view text );
		void setTextInputCallback( TextInputDelegate callback ) { _onTextInput = std::move( callback ); }

	private:
		static constexpr size_t kKeyCount  = static_cast<size_t>( Key::Count );
		static constexpr size_t kWordCount = ( kKeyCount + 63 ) / 64;

		uint64				   _arrKeyMask[kWordCount];		 /**< 이번 프레임의 키 눌림 비트마스크 (Key 인덱스 / 64 = word, % 64 = bit). */
		uint64				   _arrPressedMask[kWordCount];	 /**< 이번 프레임에 새로 눌린 키 비트마스크 (엣지). onFrameBegin/onFrameEnd에서 초기화. */
		uint64				   _arrReleasedMask[kWordCount]; /**< 이번 프레임에 새로 떼어진 키 비트마스크 (엣지). */
		TextInputDelegate	   _onTextInput;
		uint8				   _bAnyKeyPressed : 1; /**< 이번 프레임에 어떤 키든 새로 눌렸는지. wasAnyKeyPressed()가 참조. */
		[[maybe_unused]] uint8 _reserved	   : 7;
	};
} // namespace sw
