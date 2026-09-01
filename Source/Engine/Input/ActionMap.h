/**
 * @file ActionMap.h
 * @brief 상용 AAA급 Unified Enhanced ActionMap:
 *        통합 ActionEntry/ActionBinding(단일 슬롯, 1D/2D 합성축, 스틱, 조합키),
 *        완전한 ActionPhase 상태 머신(Started, Ongoing, Triggered, Completed, Canceled),
 *        모디파이어 파이프라인(데드존, 축반전, 감도배율), LIFO 컨텍스트 스택,
 *        이벤트 델리게이트 디스패치, 선입력 버퍼링, 격투 커맨드 시퀀스 및 XML 직렬화.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
	class InputManager;

	// ------------------------------------------------------------------------------
	// 1) 스키마 — 액션 값 타입 / 바인딩 종류 / 트리거 / 페이즈 / 데드존 모드
	// ------------------------------------------------------------------------------
	enum class InputActionValueType : uint8
	{
		Boolean = 0, ///< 디지털 버튼 / On-Off
		Axis1D,		 ///< 1차원 아날로그 축 [-1.0, +1.0]
		Axis2D		 ///< 2차원 평면 이동/시점 벡터
	};

	enum class BindingKind : uint8
	{
		SingleSlot = 0,	   ///< 단일 Key / MouseButton / GamepadButton
		Axis1DComposite,   ///< 1D 축 합성 (Negative Slot: -1.0, Positive Slot: +1.0)
		Vector2DComposite, ///< 2D 벡터 합성 (Up, Down, Left, Right 4방향)
		GamepadStick2D,	   ///< 게임패드 아날로그 스틱 (Left / Right)
		Chord			   ///< 조합 키 (Modifier Slot + Trigger Slot)
	};

	enum class ActionTrigger : uint8
	{
		Pressed = 0,
		Released,
		Down,
		DoubleClicked,
		HoldThreshold,
		HoldAndRelease, ///< 임계 시간 이상 누르고 있다가 뗀 순간 발화 (차지 샷)
		Tap,			///< 짧게 눌렀다 뗀 순간 발화 (회피/대시)
		Pulse,			///< 누르고 있는 동안 주기적으로 반복 발화 (연사/UI 스크롤)
		DoubleTap,		///< 같은 키를 짧은 시간 내에 2번 탭했을 때 발화
		Count
	};

	enum class ActionPhase : uint8
	{
		None = 0,
		Started,   ///< 최초 입력 감지 (0.0 ➡️ Non-Zero / Down 시작)
		Ongoing,   ///< 입력 지속 및 트리거 대기 (Hold 진행 중)
		Triggered, ///< 트리거 조건 달성 순간 발화 (Performed)
		Completed, ///< 트리거 발화 후 입력 정상 해제
		Canceled   ///< 트리거 미달성 상태에서 입력 조기 해제 (예: Hold 시간 미달 취소)
	};

	enum class GamepadStick : uint8
	{
		Left  = 0,
		Right = 1
	};

	enum class DeadzoneShape : uint8
	{
		Radial = 0, ///< 360도 원형 거리 기반 데드존 (자유 이동)
		Axial		///< X/Y 독립 십자형 데드존 (4방향 그리드/플랫포머)
	};

	namespace ActionMapDefaults
	{
		inline constexpr string_view kDefaultLayerName		 = "Gameplay";
		inline constexpr string_view kTitleLayerName		 = "Title";
		inline constexpr string_view kDebugLayerName		 = "Debug";
		inline constexpr string_view kReloadShadersAction	 = "ReloadShaders";
		inline constexpr string_view kReloadEditorAction	 = "ReloadEditor";
		inline constexpr string_view kReloadGameAction		 = "ReloadGame";
		inline constexpr string_view kQuickSaveAction		 = "QuickSave";
		inline constexpr string_view kQuickLoadAction		 = "QuickLoad";
		inline constexpr float32	 kDoubleClickTime		 = 0.35f;
		inline constexpr float32	 kDoubleClickMaxDistance = 6.0f;
		inline constexpr float32	 kHoldThreshold			 = 0.4f;
		inline constexpr float32	 kTapMaxTime			 = 0.2f;
		inline constexpr float32	 kPulseInterval			 = 0.1f;
		inline constexpr float32	 kDoubleClickTimeMin	 = 0.05f;
		inline constexpr float32	 kDoubleClickTimeMax	 = 2.0f;
		inline constexpr float32	 kDoubleClickDistanceMax = 64.0f;
		inline constexpr float32	 kHoldThresholdMin		 = 0.05f;
		inline constexpr float32	 kHoldThresholdMax		 = 10.0f;
		inline constexpr float32	 kNeverPressedSentinel	 = 1.0e9f;
	} // namespace ActionMapDefaults

	/// @brief XML 레이어: 우선순위, enabled, blockLower, alwaysOn
	struct LayerDef
	{
		string				   _name;
		int32				   _priority;
		uint8				   _bEnabled	: 1;
		uint8				   _bBlockLower : 1; /**< 켜져 있으면 낮은 우선순위/하위 스택 레이어를 비활성화합니다. */
		uint8				   _bAlwaysOn	: 1; /**< 스택이나 enableOnlyLayer에 영향받지 않고 항상 활성. */
		[[maybe_unused]] uint8 _reserved	: 5;

		LayerDef()
			: _name{}
			, _priority{ 0 }
			, _bEnabled{ SW_TRUE }
			, _bBlockLower{ SW_FALSE }
			, _bAlwaysOn{ SW_FALSE }
			, _reserved{ 0 } {}
	};

	/**
	 * @struct ActionBinding
	 * @brief 단일/합성/스틱/조합키를 모두 수용하는 통합 바인딩 구조체
	 */
	struct ActionBinding
	{
		string					_layer{};
		BindingKind				_kind{ BindingKind::SingleSlot };
		ActionTrigger			_trigger{ ActionTrigger::Pressed };
		InputSlot				_arrSlot[4]{}; /**< SingleSlot[0], Axis1D[0=Neg,1=Pos], Vector2D[0=Up,1=Down,2=Left,3=Right], Chord[0=Mod,1=Trig] */
		GamepadStick			_stick{ GamepadStick::Left };
		float32					_deadzone{ 0.0f };
		float32					_scale{ 1.0f };
		mutable const LayerDef* _pCachedLayer{ nullptr };

		ActionBinding() = default;
	};

	/**
	 * @class ActionMap
	 * @brief 상용 엔진급 Unified Enhanced Action Map
	 */
	class SW_API ActionMap
	{
	public:
		using LayerDef				   = sw::LayerDef;
		using ActionCallbackDelegate   = Delegate<void()>;
		using PhaseCallbackDelegate	   = Delegate<void( ActionPhase )>;
		using Vector2DCallbackDelegate = Delegate<void( float2 )>;

		ActionMap();

		// ------------------------------------------------------------------------------
		// 2) 로드 & 수명주기
		// ------------------------------------------------------------------------------
		/** @brief 액션·레이어·바인딩을 모두 비웁니다. */
		void clear();
		/** @brief 리소스 상대 경로에서 InputMap XML을 로드합니다. */
		bool loadFromResource( string_view relativePath );
		/** @brief 엔진 기본 폴백 바인딩을 등록합니다 (Ctrl+F6/F7/F8, WASD, Space 등). */
		void bindDefaultFallback();
		/** @brief 한 프레임의 입력 상태를 평가하고 델리게이트를 디스패치합니다. */
		void update( float32 deltaSeconds );

		// ------------------------------------------------------------------------------
		// 3) 통합 바인딩 API — InputSlot / 키 / 패드 / 마우스 / 1D 합성 / 2D 합성 / 스틱 / 조합키
		// ------------------------------------------------------------------------------
		void bind( string_view action, InputSlot slot, ActionTrigger trigger = ActionTrigger::Pressed, string_view layer = {} );
		void bind( string_view action, Key key, ActionTrigger trigger = ActionTrigger::Pressed, string_view layer = {} );
		void bind( string_view action, GamepadButton button, ActionTrigger trigger = ActionTrigger::Pressed, string_view layer = {} );
		void bind( string_view action, MouseButton mouse, ActionTrigger trigger = ActionTrigger::Pressed, string_view layer = {} );

		/** @brief 1D 축 합성 바인딩 (negativeKey: -1.0, positiveKey: +1.0). */
		void bindAxis1DComposite( string_view action, Key negativeKey, Key positiveKey, string_view layer = {} );
		/** @brief 1D 축 값을 반환합니다 [-1.0, +1.0]. */
		float32 getAxis1D( string_view action ) const;
		float32 getAxis1D( const hashed_string& action ) const;

		/** @brief 2D 이동 축 바인딩을 등록합니다 (키보드 4방향 합성). */
		void bindVector2D( string_view action, Key up, Key down, Key left, Key right, float32 deadzone = 0.0f, string_view layer = {} );
		/** @brief 게임패드 아날로그 스틱 2D 이동 축 바인딩을 등록합니다. */
		void bindGamepadStick2D( string_view action, GamepadStick stick = GamepadStick::Left, float32 deadzone = 0.15f, string_view layer = {} );
		/** @brief 2D 이동 벡터를 반환합니다 (데드존, 감도, 축 반전 적용). */
		float2 getVector2D( string_view action ) const;
		float2 getVector2D( const hashed_string& action ) const;

		/** @brief 조합 키(Chord) 바인딩을 등록합니다 (예: Ctrl + F7, Shift + W). */
		void bindChord( string_view action, Key modifierKey, Key triggerKey, ActionTrigger trigger = ActionTrigger::Pressed, string_view layer = {} );
		bool isChordDown( string_view action ) const;
		bool isChordDown( const hashed_string& action ) const;
		bool wasChordTriggered( string_view action ) const;
		bool wasChordTriggered( const hashed_string& action ) const;

		// ------------------------------------------------------------------------------
		// 3.1) 이벤트 드리븐 델리게이트 바인딩
		// ------------------------------------------------------------------------------
		void bindActionCallback( string_view action, ActionTrigger trigger, ActionCallbackDelegate callback );
		void bindPhaseCallback( string_view action, ActionPhase phase, ActionCallbackDelegate callback );
		void bindVector2DCallback( string_view action, Vector2DCallbackDelegate callback );
		void clearCallbacks();

		// ------------------------------------------------------------------------------
		// 4) LIFO 컨텍스트 스택 & 모달/비모달 제어
		// ------------------------------------------------------------------------------
		void registerLayer( string_view name, int32 priority = 0, bool enabled = true, bool blockLower = false, bool alwaysOn = false );
		void setLayerEnabled( string_view layer, bool enabled );
		/** @brief LIFO 레이어 스택 상단에 레이어를 푸시합니다 (blockLower=false: 비모달 동시 조작, true: 모달 차단). */
		void pushLayer( string_view layer, bool blockLower = false, bool showCursor = false );
		/** @brief LIFO 레이어 스택 상단의 레이어를 팝합니다. */
		void popLayer();
		/** @brief LIFO 레이어 스택에서 특정 레이어를 찾아 팝합니다. */
		void popLayer( string_view layer );
		/** @brief 현재 LIFO 스택 최상단 레이어 이름을 반환합니다. */
		string_view getCurrentTopLayer() const;
		void		enableOnlyLayer( string_view layer );

		// ------------------------------------------------------------------------------
		// 5) 감도, 축 반전, 데드존 모드 & 접근성 설정
		// ------------------------------------------------------------------------------
		void		  setInvertX( bool invert ) { _bInvertX = invert ? SW_TRUE : SW_FALSE; }
		void		  setInvertY( bool invert ) { _bInvertY = invert ? SW_TRUE : SW_FALSE; }
		bool		  isInvertX() const { return _bInvertX == SW_TRUE; }
		bool		  isInvertY() const { return _bInvertY == SW_TRUE; }
		void		  setMouseSensitivity( float2 sens ) { _mouseSensitivity = sens; }
		float2		  getMouseSensitivity() const { return _mouseSensitivity; }
		void		  setGamepadSensitivity( float2 sens ) { _gamepadSensitivity = sens; }
		float2		  getGamepadSensitivity() const { return _gamepadSensitivity; }
		void		  setDeadzoneShape( DeadzoneShape shape ) { _deadzoneShape = shape; }
		DeadzoneShape getDeadzoneShape() const { return _deadzoneShape; }

		/** @brief 액션의 토글 모드를 켜거나 끕니다 (접근성: 조준/달리기 한 번 눌러 켜기). */
		void setToggleMode( string_view action, bool bToggle );
		bool isActionToggled( string_view action ) const;
		bool isActionToggled( const hashed_string& action ) const;

		void setNavRepeatDelay( float32 delay ) { _navRepeatDelay = delay; }
		void setNavRepeatRate( float32 rate ) { _navRepeatRate = rate; }

		// ------------------------------------------------------------------------------
		// 6) 액션 선입력 버퍼링 & 격투 커맨드 판정
		// ------------------------------------------------------------------------------
		/** @brief 후딜레이 중 입력된 액션을 일정 시간 동안 보존합니다 (0.2s 기본값). */
		void bufferAction( string_view action, float32 expirationSeconds = 0.2f );
		/** @brief 선입력 버퍼에 보존된 액션이 있으면 소비하고 true를 반환합니다. */
		bool consumeBufferedAction( string_view action );

		/** @brief 지정된 시간 윈도우 내에 연속된 커맨드 시퀀스(예: ["Down", "DownRight", "Right", "Attack"])가 성공했는지 검사합니다. */
		bool checkCommandSequence( const vector<string>& listSequence, float32 maxWindowSeconds = 0.35f ) const;

		// ------------------------------------------------------------------------------
		// 7) UI 글리프 조회 & 인게임 키 리매핑 직렬화
		// ------------------------------------------------------------------------------
		/** @brief 현재 활성 입력 장치에 맞는 UI 안내 글리프 문자열(예: "[ E ]" 또는 "[ Ⓨ ]")을 반환합니다. */
		string getGlyphForAction( string_view action ) const;

		/** @brief 런타임에 액션의 키 바인딩을 변경합니다. */
		bool rebindKey( string_view action, Key newKey, uint32 bindIndex = 0 );
		bool rebindSlot( string_view action, InputSlot slot, uint32 bindIndex = 0 );
		/** @brief 유저 키 바인딩을 XML 파일로 저장합니다. */
		bool saveUserBindings( string_view filePath ) const;
		/** @brief 저장된 유저 키 바인딩 XML 파일을 로드하여 적용합니다. */
		bool loadUserBindings( string_view filePath );

		// ------------------------------------------------------------------------------
		// 8) 입력 장치 연결 및 임계값
		// ------------------------------------------------------------------------------
		void		  setInputManager( InputManager* pInput ) { _pInput = pInput; }
		void		  setDoubleClickTime( float32 seconds );
		void		  setDoubleClickMaxDistance( float32 pixels );
		void		  setHoldThreshold( float32 seconds );
		InputManager* getInputManager() const { return _pInput; }
		float32		  getDoubleClickTime() const { return _doubleClickTime; }
		float32		  getHoldThreshold() const { return _holdThreshold; }

		// ------------------------------------------------------------------------------
		// 9) 조회 & 프레임 상태 (통합 쿼리)
		// ------------------------------------------------------------------------------
		bool				  hasLayer( string_view layer ) const;
		bool				  isLayerEnabled( string_view layer ) const;
		int32				  getLayerPriority( string_view layer ) const;
		const vector<string>& getLayerNames() const { return _listLayerName; }
		const string&		  getDefaultLayerName() const { return _defaultLayerName; }
		bool				  hasAction( string_view action ) const;
		const vector<string>& getActionNames() const { return _listActionName; }
		ActionTrigger		  getBindingTrigger( string_view action, uint32 bindIndex ) const;
		uint32				  getBindingCount( string_view action ) const;
		const ActionBinding*  getBinding( string_view action, uint32 bindIndex ) const;

		bool		wasActionTriggered( string_view action ) const;
		bool		wasActionTriggered( const hashed_string& action ) const;
		bool		isActionDown( string_view action ) const;
		bool		isActionDown( const hashed_string& action ) const;
		bool		wasActionPressed( string_view action ) const;
		bool		wasActionPressed( const hashed_string& action ) const;
		bool		wasActionReleased( string_view action ) const;
		bool		wasActionReleased( const hashed_string& action ) const;
		bool		wasActionDoubleClicked( string_view action ) const;
		bool		wasActionDoubleClicked( const hashed_string& action ) const;
		bool		wasActionHoldThreshold( string_view action ) const;
		bool		wasActionHoldThreshold( const hashed_string& action ) const;
		float32		getActionHoldDuration( string_view action ) const;
		float32		getActionHoldDuration( const hashed_string& action ) const;
		ActionPhase getActionPhase( string_view action ) const;
		ActionPhase getActionPhase( const hashed_string& action ) const;

		bool				 isPointerHovering() const;
		bool				 wasPointerHoverEntered() const;
		bool				 wasPointerHoverLeft() const;
		bool				 isPointerOverRect( int32 x, int32 y, int32 w, int32 h ) const;
		static ActionTrigger actionTriggerFromName( string_view name );
		static const utf8*	 actionTriggerToName( ActionTrigger trigger );

	private:
		struct ActionBindingState
		{
			float32				   _holdDuration;
			float32				   _timeSinceLastPress;
			float32				   _pulseTimer;
			int32				   _lastPressX;
			int32				   _lastPressY;
			uint8				   _bDown		   : 1;
			uint8				   _bPressed	   : 1;
			uint8				   _bReleased	   : 1;
			uint8				   _bDoubleClicked : 1;
			uint8				   _bHoldThreshold : 1;
			uint8				   _bWasDown	   : 1;
			uint8				   _bTriggered	   : 1;
			[[maybe_unused]] uint8 _reserved	   : 1;

			ActionBindingState()
				: _holdDuration{ 0.0f }
				, _timeSinceLastPress{ ActionMapDefaults::kNeverPressedSentinel }
				, _pulseTimer{ 0.0f }
				, _lastPressX{ 0 }
				, _lastPressY{ 0 }
				, _bDown{ SW_FALSE }
				, _bPressed{ SW_FALSE }
				, _bReleased{ SW_FALSE }
				, _bDoubleClicked{ SW_FALSE }
				, _bHoldThreshold{ SW_FALSE }
				, _bWasDown{ SW_FALSE }
				, _bTriggered{ SW_FALSE }
				, _reserved{ 0 } {}
		};

		struct ActionEntry
		{
			InputActionValueType	   _valueType{ InputActionValueType::Boolean };
			vector<ActionBinding>	   _listBinding{};
			vector<ActionBindingState> _listBindingState{};
			float2					   _currentValue{ 0.0f, 0.0f };
			float32					   _holdDuration{ 0.0f };
			ActionPhase				   _currentPhase{ ActionPhase::None };
			uint8					   _bDown		   : 1;
			uint8					   _bPressed	   : 1;
			uint8					   _bReleased	   : 1;
			uint8					   _bDoubleClicked : 1;
			uint8					   _bHoldThreshold : 1;
			uint8					   _bTriggered	   : 1;
			[[maybe_unused]] uint8	   _reserved	   : 2;

			ActionEntry()
				: _valueType{ InputActionValueType::Boolean }
				, _listBinding{}
				, _listBindingState{}
				, _currentValue{ 0.0f, 0.0f }
				, _holdDuration{ 0.0f }
				, _currentPhase{ ActionPhase::None }
				, _bDown{ SW_FALSE }
				, _bPressed{ SW_FALSE }
				, _bReleased{ SW_FALSE }
				, _bDoubleClicked{ SW_FALSE }
				, _bHoldThreshold{ SW_FALSE }
				, _bTriggered{ SW_FALSE }
				, _reserved{ 0 } {}
		};

		struct ActionCallbackEntry
		{
			ActionTrigger		   _trigger{ ActionTrigger::Pressed };
			ActionCallbackDelegate _callback;
		};

		struct PhaseCallbackEntry
		{
			ActionPhase			   _phase{ ActionPhase::None };
			ActionCallbackDelegate _callback;
		};

		struct BufferedActionItem
		{
			string	_action{};
			float32 _remainingTime{ 0.0f };
		};

		struct CommandHistoryEntry
		{
			string	_action{};
			float32 _timestamp{ 0.0f };
		};

		bool			   evaluateBindingDown( const ActionBinding& binding, float2& outValue ) const;
		bool			   evaluateTrigger( ActionTrigger trigger, const ActionBindingState& state, float32 deltaSeconds ) const;
		bool			   isBindingLayerActive( const ActionBinding& binding ) const;
		bool			   isLayerActiveInternal( string_view layer ) const;
		void			   ensureActionListed( string_view action );
		LayerDef&		   ensureLayer( string_view name, int32 priority = 0, bool enabled = true, bool blockLower = false, bool alwaysOn = false );
		ActionEntry&	   getOrCreateAction( string_view action, InputActionValueType valueType = InputActionValueType::Boolean );
		LayerDef*		   findLayer( string_view name );
		const LayerDef*	   findLayer( string_view name ) const;
		ActionEntry*	   findAction( string_view action );
		const ActionEntry* findAction( string_view action ) const;
		const ActionEntry* findAction( const hashed_string& action ) const;

	private:
		InputManager*											   _pInput;
		map<string, ActionEntry, std::less<>>					   _mapAction;
		map<string, LayerDef, std::less<>>						   _mapLayer;
		map<string, vector<ActionCallbackEntry>, std::less<>>	   _mapActionCallback;
		map<string, vector<PhaseCallbackEntry>, std::less<>>	   _mapPhaseCallback;
		map<string, vector<Vector2DCallbackDelegate>, std::less<>> _mapVector2DCallback;
		map<string, bool, std::less<>>							   _mapToggleMode;
		map<string, bool, std::less<>>							   _mapToggleState;
		vector<string>											   _listActionName;
		vector<string>											   _listLayerName;
		vector<string>											   _listLayerStack;
		vector<BufferedActionItem>								   _listBufferedAction;
		vector<CommandHistoryEntry>								   _listCommandHistory;
		string													   _defaultLayerName;
		float2													   _mouseSensitivity;
		float2													   _gamepadSensitivity;
		float32													   _doubleClickTime;
		float32													   _doubleClickMaxDistance;
		float32													   _holdThreshold;
		float32													   _navRepeatDelay;
		float32													   _navRepeatRate;
		float32													   _totalElapsedTime;
		DeadzoneShape											   _deadzoneShape;
		uint8													   _bInvertX	  : 1;
		uint8													   _bInvertY	  : 1;
		[[maybe_unused]] uint8									   _reservedFlags : 6;
	};
} // namespace sw
