/**
 * @file ActionMap.h
 * @brief InputMap XML: 액션, 바인드별 트리거, 이름 있는 레이어 (리소스 기반).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
	class InputManager;

	// ------------------------------------------------------------------------------
	// 1) 스키마 — 바인딩 소스 / 트리거 / 페이즈, XML 클램프 기본값
	// ------------------------------------------------------------------------------
	enum class InputBindingSource : uint8
	{
		Key = 0,
		GamepadButton,
		MouseButton
	};

	enum class ActionTrigger : uint8
	{
		Pressed = 0,
		Released,
		Down,
		DoubleClicked,
		HoldThreshold,
		Count
	};

	enum class ActionPhase : uint8
	{
		None = 0,
		Started,
		Performed,
		Canceled
	};

	enum class GamepadStick : uint8
	{
		Left  = 0,
		Right = 1
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
		inline constexpr float32	 kDoubleClickTimeMin	 = 0.05f;
		inline constexpr float32	 kDoubleClickTimeMax	 = 2.0f;
		inline constexpr float32	 kDoubleClickDistanceMax = 64.0f;
		inline constexpr float32	 kHoldThresholdMin		 = 0.05f;
		inline constexpr float32	 kHoldThresholdMax		 = 10.0f;
		inline constexpr float32	 kNeverPressedSentinel	 = 1.0e9f;
	} // namespace ActionMapDefaults

	struct InputBinding
	{
		string			   _layer{};
		Key				   _key{ Key::Unknown };
		GamepadButton	   _button{ GamepadButton::Count };
		MouseButton		   _mouse{ MouseButton::Count };
		InputBindingSource _source{ InputBindingSource::Key };
		ActionTrigger	   _trigger{ ActionTrigger::Pressed };

		InputBinding() = default;
	};

	/**

	 * @class ActionMap
	 * @brief InputMap XML에서 카탈로그·레이어·트리거를 모두 로드합니다.
	 * @note InputManager::beginFrame 이후 update(dt)를 호출하세요. setLayerEnabled / pushLayer / popLayer를 사용합니다.
	 */
	class SW_API ActionMap
	{
	public:
		/** @brief 빈 맵으로 시작합니다. */
		ActionMap();

		// ------------------------------------------------------------------------------
		// 2) 로드 — XML 카탈로그, 비상 폴백, 매 프레임 update (beginFrame 이후)
		// ------------------------------------------------------------------------------
		/** @brief 액션·레이어·바인딩을 모두 비웁니다. */
		void clear();
		/** @brief 리소스 상대 경로에서 InputMap XML을 로드합니다. */
		bool loadFromResource( string_view relativePath );
		/** @brief 엔진 비상 입력 맵을 바인딩합니다. */
		void bindEmergencyFallback();
		/** @brief 한 프레임의 입력 상태를 갱신합니다. */
		void update( float32 deltaSeconds );

		// ------------------------------------------------------------------------------
		// 3) 바인딩 — 키 / 패드 / 마우스. layer 비우면 기본 레이어
		// ------------------------------------------------------------------------------
		/** @brief 키 바인딩을 등록합니다. */
		void bind( string_view action, Key key, ActionTrigger trigger = ActionTrigger::Pressed,
				   string_view layer = {} );
		/** @brief 게임패드 버튼 바인딩을 등록합니다. */
		void bind( string_view action, GamepadButton button, ActionTrigger trigger = ActionTrigger::Pressed,
				   string_view layer = {} );
		/** @brief 마우스 버튼 바인딩을 등록합니다. */
		void bind( string_view action, MouseButton mouse, ActionTrigger trigger = ActionTrigger::Pressed,
				   string_view layer = {} );

		// ------------------------------------------------------------------------------
		// 3.1) 2D 벡터 축 및 조합 키(Chord) 바인딩
		// ------------------------------------------------------------------------------
		/** @brief 2D 이동 축 바인딩을 등록합니다 (키보드 4방향). */
		void bindVector2D( string_view action, Key up, Key down, Key left, Key right, float32 deadzone = 0.0f,
						   string_view layer = {} );
		/** @brief 게임패드 아날로그 스틱 2D 이동 축 바인딩을 등록합니다. */
		void bindGamepadStick2D( string_view action, GamepadStick stick = GamepadStick::Left, float32 deadzone = 0.15f,
								 string_view layer = {} );
		/** @brief 2D 이동 벡터를 반환합니다 (정규화 및 데드존 적용). */
		float2 getVector2D( string_view action ) const;
		float2 getVector2D( const hashed_string& action ) const;

		/** @brief 조합 키(Chord) 바인딩을 등록합니다 (예: Ctrl + S, Shift + W). */
		void bindChord( string_view action, Key modifierKey, Key triggerKey, ActionTrigger trigger = ActionTrigger::Pressed,
						string_view layer = {} );
		/** @brief 조합 키가 현재 눌린 상태인지 반환합니다. */
		bool isChordDown( string_view action ) const;
		bool isChordDown( const hashed_string& action ) const;
		/** @brief 이번 프레임에 조합 키가 발화되었는지 반환합니다. */
		bool wasChordTriggered( string_view action ) const;
		bool wasChordTriggered( const hashed_string& action ) const;

		// ------------------------------------------------------------------------------
		// 4) 레이어 — priority / enabled / blockLower / alwaysOn
		//    enableOnlyLayer는 alwaysOn을 끄지 않음
		// ------------------------------------------------------------------------------
		/** @brief XML 로드 중 레이어를 확보합니다. */
		void registerLayer( string_view name, int32 priority = 0, bool enabled = true, bool blockLower = false,
							bool alwaysOn = false );
		/** @brief 레이어 활성 여부를 설정합니다. */
		void setLayerEnabled( string_view layer, bool enabled );
		/** @brief 레이어를 켭니다. */
		void pushLayer( string_view layer );
		/** @brief 레이어를 끕니다. */
		void popLayer( string_view layer );
		/** @brief alwaysOn을 제외한 레이어를 끄고 하나만 켭니다. */
		void enableOnlyLayer( string_view layer );

		// ------------------------------------------------------------------------------
		// 5) 입력 장치 · 더블클릭/홀드 임계
		// ------------------------------------------------------------------------------
		/** @brief InputManager를 연결합니다. */
		void setInputManager( InputManager* pInput ) { _pInput = pInput; }
		/** @brief 더블클릭 판정 시간을 설정합니다. */
		void setDoubleClickTime( float32 seconds );
		/** @brief 더블클릭 허용 거리를 설정합니다. */
		void setDoubleClickMaxDistance( float32 pixels );
		/** @brief 홀드 임계 시간을 설정합니다. */
		void setHoldThreshold( float32 seconds );
		/** @brief 연결된 InputManager를 반환합니다. */
		InputManager* getInputManager() const { return _pInput; }
		/** @brief 더블클릭 판정 시간을 반환합니다. */
		float32 getDoubleClickTime() const { return _doubleClickTime; }
		/** @brief 홀드 임계 시간을 반환합니다. */
		float32 getHoldThreshold() const { return _holdThreshold; }

		// ------------------------------------------------------------------------------
		// 6) 조회 — 레이어 / 액션 카탈로그
		// ------------------------------------------------------------------------------
		/** @brief 레이어가 등록되어 있는지 반환합니다. */
		bool hasLayer( string_view layer ) const;
		/** @brief 레이어가 켜져 있는지 반환합니다. */
		bool isLayerEnabled( string_view layer ) const;
		/** @brief 레이어 우선순위를 반환합니다. */
		int32 getLayerPriority( string_view layer ) const;
		/** @brief 등록된 레이어 이름 목록을 반환합니다. */
		const vector<string>& getLayerNames() const { return _listLayerName; }
		/** @brief 기본 레이어 이름을 반환합니다. */
		const string& getDefaultLayerName() const { return _defaultLayerName; }
		/** @brief 액션이 등록되어 있는지 반환합니다. */
		bool hasAction( string_view action ) const;
		/** @brief 등록된 액션 이름 목록을 반환합니다. */
		const vector<string>& getActionNames() const { return _listActionName; }
		/** @brief 액션의 바인딩 트리거를 반환합니다. */
		ActionTrigger getBindingTrigger( string_view action, uint32 bindIndex ) const;
		/** @brief 액션의 바인딩 개수를 반환합니다. */
		uint32 getBindingCount( string_view action ) const;

		// ------------------------------------------------------------------------------
		// 7) 프레임 상태 — 레이어가 활성일 때만 발화
		// ------------------------------------------------------------------------------
		/** @brief 바인딩이 트리거를 발화했고 해당 레이어가 현재 활성일 때 true. */
		bool wasActionTriggered( string_view action ) const;
		bool wasActionTriggered( const hashed_string& action ) const;
		/** @brief 액션이 눌린 상태인지 반환합니다. */
		bool isActionDown( string_view action ) const;
		bool isActionDown( const hashed_string& action ) const;
		/** @brief 이번 프레임에 액션이 눌렸는지 반환합니다. */
		bool wasActionPressed( string_view action ) const;
		bool wasActionPressed( const hashed_string& action ) const;
		/** @brief 이번 프레임에 액션이 떼어졌는지 반환합니다. */
		bool wasActionReleased( string_view action ) const;
		bool wasActionReleased( const hashed_string& action ) const;
		/** @brief 이번 프레임에 더블클릭이 발생했는지 반환합니다. */
		bool wasActionDoubleClicked( string_view action ) const;
		bool wasActionDoubleClicked( const hashed_string& action ) const;
		/** @brief 홀드 임계를 넘겼는지 반환합니다. */
		bool wasActionHoldThreshold( string_view action ) const;
		bool wasActionHoldThreshold( const hashed_string& action ) const;
		/** @brief 액션 홀드 지속 시간을 반환합니다. */
		float32 getActionHoldDuration( string_view action ) const;
		float32 getActionHoldDuration( const hashed_string& action ) const;
		/** @brief 액션 페이즈를 반환합니다. */
		ActionPhase getActionPhase( string_view action ) const;
		ActionPhase getActionPhase( const hashed_string& action ) const;

		// ------------------------------------------------------------------------------

		// 8) 포인터 히트 · 트리거 이름 변환
		// ------------------------------------------------------------------------------
		/** @brief 포인터가 호버 중인지 반환합니다. */
		bool isPointerHovering() const;
		/** @brief 이번 프레임에 호버가 시작됐는지 반환합니다. */
		bool wasPointerHoverEntered() const;
		/** @brief 이번 프레임에 호버가 끝났는지 반환합니다. */
		bool wasPointerHoverLeft() const;
		/** @brief 포인터가 사각형 위에 있는지 반환합니다. */
		bool isPointerOverRect( int32 x, int32 y, int32 w, int32 h ) const;
		/** @brief 이름에서 ActionTrigger를 해석합니다. */
		static ActionTrigger actionTriggerFromName( string_view name );
		/** @brief ActionTrigger의 안정 이름을 반환합니다. */
		static const utf8* actionTriggerToName( ActionTrigger trigger );

	private:
		/// @brief XML 레이어: 우선순위, enabled, blockLower, alwaysOn
		struct LayerDef
		{
			string				   _name;
			int32				   _priority;
			uint8				   _bEnabled	: 1;
			uint8				   _bBlockLower : 1; /**< 켜져 있으면 낮은 우선순위 레이어를 비활성화합니다. */
			uint8				   _bAlwaysOn	: 1; /**< enableOnlyLayer가 끄지 않습니다. */
			[[maybe_unused]] uint8 _reserved	: 5;

			/** @brief 기본 레이어 정의입니다. */
			LayerDef()
				: _name{}
				, _priority{ 0 }
				, _bEnabled{ 1 }
				, _bBlockLower{ 0 }
				, _bAlwaysOn{ 0 }
				, _reserved{ 0 } {}
		};

		/// @brief 바인딩 한 줄의 프레임 상태 (다운/프레스/홀드/더블클릭)
		struct BindingState
		{
			float32				   _holdDuration;
			float32				   _timeSinceLastPress;
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

			/** @brief 기본 바인딩 상태입니다. */
			BindingState()
				: _holdDuration{ 0.0f }
				, _timeSinceLastPress{ ActionMapDefaults::kNeverPressedSentinel }
				, _lastPressX{ 0 }
				, _lastPressY{ 0 }
				, _bDown{ 0 }
				, _bPressed{ 0 }
				, _bReleased{ 0 }
				, _bDoubleClicked{ 0 }
				, _bHoldThreshold{ 0 }
				, _bWasDown{ 0 }
				, _bTriggered{ 0 }
				, _reserved{ 0 } {}
		};

		/// @brief 액션별 바인딩 목록과 이번 프레임 합산 상태
		struct ActionRuntime
		{
			vector<InputBinding>   _listBinding;
			vector<BindingState>   _listBindState;
			float32				   _holdDuration;
			uint8				   _bDown		   : 1;
			uint8				   _bPressed	   : 1;
			uint8				   _bReleased	   : 1;
			uint8				   _bDoubleClicked : 1;
			uint8				   _bHoldThreshold : 1;
			uint8				   _bTriggered	   : 1;
			[[maybe_unused]] uint8 _reserved	   : 2;

			/** @brief 기본 액션 런타임입니다. */
			ActionRuntime()
				: _listBinding{}
				, _listBindState{}
				, _holdDuration{ 0.0f }
				, _bDown{ 0 }
				, _bPressed{ 0 }
				, _bReleased{ 0 }
				, _bDoubleClicked{ 0 }
				, _bHoldThreshold{ 0 }
				, _bTriggered{ 0 }
				, _reserved{ 0 } {}
		};

		/** @brief 바인딩이 현재 눌린 상태인지 평가합니다. */
		bool evaluateDown( const InputBinding& binding ) const;
		/** @brief 트리거 조건이 충족됐는지 평가합니다. */
		bool evaluateTrigger( ActionTrigger trigger, const BindingState& state ) const;
		/** @brief 바인딩의 레이어가 현재 활성인지 반환합니다. */
		bool isBindingLayerActive( const InputBinding& binding ) const;
		/** @brief blockLower가 켜진 레이어의 차단 하한 우선순위를 계산합니다. */
		int32 computeBlockFloorPriority() const;
		/** @brief 액션 이름 목록에 없으면 추가합니다. */
		void ensureActionListed( string_view action );
		/** @brief 레이어를 찾거나 새로 만듭니다. */
		LayerDef& ensureLayer( string_view name, int32 priority = 0, bool enabled = true,
							   bool blockLower = false, bool alwaysOn = false );
		/** @brief 액션 런타임을 찾거나 새로 만듭니다. */
		ActionRuntime& getOrCreateRuntime( string_view action );
		/** @brief 이름으로 레이어를 찾습니다. */
		LayerDef* findLayer( string_view name );
		/** @brief 이름으로 레이어를 찾습니다. */
		const LayerDef* findLayer( string_view name ) const;
		/** @brief 이름으로 액션 런타임을 찾습니다. */
		ActionRuntime* findRuntime( string_view action );
		/** @brief 이름으로 액션 런타임을 찾습니다. */
		const ActionRuntime* findRuntime( string_view action ) const;

		struct Vector2DBinding
		{
			string	_layer{};
			Key		_up{ Key::Unknown };
			Key		_down{ Key::Unknown };
			Key		_left{ Key::Unknown };
			Key		_right{ Key::Unknown };
			float32 _deadzone{ 0.0f };
		};

		struct GamepadStickBinding
		{
			string		 _layer{};
			GamepadStick _stick{ GamepadStick::Left };
			float32		 _deadzone{ 0.15f };
		};

		struct ChordBinding
		{
			string		  _layer{};
			Key			  _modifier{ Key::Unknown };
			Key			  _triggerKey{ Key::Unknown };
			ActionTrigger _trigger{ ActionTrigger::Pressed };
		};

	private:
		InputManager*										  _pInput;
		map<string, ActionRuntime, std::less<>>				  _mapAction;
		map<string, LayerDef, std::less<>>					  _mapLayer;
		map<string, Vector2DBinding, std::less<>>			  _mapVector2D;
		map<string, vector<GamepadStickBinding>, std::less<>> _mapGamepadStick;
		map<string, vector<ChordBinding>, std::less<>>		  _mapChord;
		vector<string>										  _listActionName;
		vector<string>										  _listLayerName;
		string												  _defaultLayerName;
		float32												  _doubleClickTime;
		float32												  _doubleClickMaxDistance;
		float32												  _holdThreshold;
		int32												  _cachedBlockFloor;
	};
} // namespace sw
