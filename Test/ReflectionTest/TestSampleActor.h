/**
 * @file TestSampleActor.h
 * @brief Reflection 테스트용 REFLECT/ENUM 샘플 타입
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) REFLECT 샘플 — 프로퍼티·별칭·기본값·중첩
	// ------------------------------------------------------------------------------
	REFLECT()
	struct SampleTestActor
	{
		PROPERTY()
		int32 _hp = 100;

		PROPERTY()
		string _name = "Hero";

		FUNCTION()
		/** @brief HP에서 피해량을 뺍니다. */
		void takeDamage( int32 damage )
		{
			_hp -= damage;
		}

		FUNCTION()
		/** @brief 현재 HP를 반환합니다. */
		int32 getHp() const
		{
			return _hp;
		}
	};

	REFLECT()
	struct AliasAndReorderTestActor
	{
		PROPERTY( Alias = "hp, HitPoints" )
		int32 _currentHp = 100;

		PROPERTY()
		int32 _score = 50;
	};

	/**
	 * @brief 타입 개명 호환 — 옛 FQN `sw::LegacyRenameActor` 로 findType / 컴포넌트 키 조회.
	 */
	REFLECT( Alias = LegacyRenameActor )
	struct RenameCompatActor
	{
		PROPERTY()
		int32 _hp = 100;
	};

	REFLECT()
	struct DefaultValueTestActor
	{
		/** @brief 에셋에 _mana 가 없으면 Default 를 적용합니다(클래스 초기값 0이 아님). */
		PROPERTY( Default = "75" )
		int32 _mana{ 0 };

		PROPERTY( Default = "Apprentice", XmlAttribute )
		string _title = "unset";
	};

	REFLECT()
	struct NestedInner
	{
		PROPERTY()
		int32 _x{ 0 };
	};

	REFLECT()
	struct NestedContainerActor
	{
		PROPERTY()
		vector<vector<int32>> _grid;

		PROPERTY()
		map<string, vector<float32>> _namedRows;

		PROPERTY()
		NestedInner _inner;
	};

	// ------------------------------------------------------------------------------
	// 2) FUNCTION / RPC / Abstract / Static / 생성자
	// ------------------------------------------------------------------------------
	REFLECT()
	struct RpcDemoActor
	{
		PROPERTY()
		int32 _hp = 100;

		FUNCTION( Server, Reliable, Category = "Combat", DisplayName = "Apply Damage",
				  Tooltip = "Subtracts amount from HP" )
		/** @brief HP에서 피해량을 뺍니다(RPC 데모). */
		void applyDamage( int32 amount )
		{
			_hp -= amount;
		}
	};

	/** @brief Unreal UCLASS(Abstract) 스타일 — 등록되지만 생성할 수 없습니다. */
	REFLECT( Abstract )
	struct AbstractDemoBase
	{
		PROPERTY()
		int32 _baseValue{ 1 };

		/** @brief 가상 소멸자. */
		virtual ~AbstractDemoBase() = default;
		/** @brief 추상 틱 훅. */
		virtual void tickAbstract() = 0;
	};

	/** @brief BlueprintFunctionLibrary 스타일의 static 헬퍼. */
	REFLECT( Static )
	struct StaticDemoLibrary
	{
		FUNCTION( Category = "Math", DisplayName = "Double Int", Tooltip = "Returns value * 2" )
		/** @brief value * 2 를 반환합니다. */
		static int32 doubleInt( int32 value )
		{
			return value * 2;
		}
	};

	REFLECT()
	struct PolyPayloadA
	{
		PROPERTY()
		int32 _a{ 1 };
	};

	REFLECT()
	struct AssetPathActor
	{
		PROPERTY( AssetPath, AssetType = "Texture" )
		string _albedo;

		PROPERTY( Polymorphic )
		ReflectAny _payload;
	};

	/** @brief 명시적 생성자로 ReflectionParser 가 `$ctor` / `$ctor(int32)` 를 출력하게 합니다. */
	REFLECT()
	struct CtorDemoActor
	{
		PROPERTY()
		int32 _value = -1;

		/** @brief `_value` 를 0으로 두는 기본 생성자. */
		CtorDemoActor()
			: _value{ 0 }
		{
		}

		/** @brief `_value` 를 인자로 초기화합니다. */
		explicit CtorDemoActor( int32 value )
			: _value{ value }
		{
		}
	};

	/** @brief 코드젠이 출력하는 PROPERTY 어노테이션 메타데이터. */
	REFLECT()
	struct MetadataDemoActor
	{
		PROPERTY( Category = "Stats", DisplayName = "Hit Points", Tooltip = "Current HP", ReadOnly )
		int32 _hp = 10;
	};

	// ------------------------------------------------------------------------------
	// 3) ENUM — 별칭·Flags·중첩 네임스페이스
	// ------------------------------------------------------------------------------
	ENUM( Alias = LegacySampleStatus, ValueAlias = "OldIdle:Idle, OldMoving:Moving" )
	enum class SampleStatus : uint8
	{
		Idle,
		Moving,
		Attacking,
	};

	namespace InnerNamespaceForTest
	{
		REFLECT()
		struct OuterStruct
		{
			PROPERTY()
			int32 _outerValue = 42;

			REFLECT()
			struct InnerStruct
			{
				PROPERTY()
				string _innerData = "NestedData";

				PROPERTY()
				float32 _score = 3.14f;
			};

			REFLECT()
			class InnerClass
			{
			public:
				PROPERTY()
				int64 _id = 999;
			};

			ENUM()
			enum class InnerEnum : uint32
			{
				OptionA = 1,
				OptionB = 2,
				OptionC = 4,
			};
		};
	} // namespace InnerNamespaceForTest

	// ------------------------------------------------------------------------------
	// 4) Component 생명주기 훅·상속
	// ------------------------------------------------------------------------------
	REFLECT()
	struct TestScriptComponent : public Component
	{
		REFLECT_BODY();

		PROPERTY()
		float32 _scriptSpeed{ 1.5f };

		/**
		 * 이 변수는 PROPERTY() 가 주석에 있지만, 실제로는 파싱되지 않아야 합니다.
		 */
		int32 _shouldNotBeParsed{ 0 };

		uint32 _tickCount{ 0 };
		bool   _beganPlay{ false };
		bool   _endedPlay{ false };

		/** @brief 플레이 시작 플래그를 켭니다. */
		void onBeginPlay() override
		{
			_beganPlay = true;
		}

		/** @brief 틱 카운트를 증가시킵니다. */
		void onTick( float32 dt ) override
		{
			(void)dt;
			_tickCount++;
		}

		/** @brief 플레이 종료 플래그를 켭니다. */
		void onEndPlay() override
		{
			_endedPlay = true;
		}
	};

	REFLECT()
	struct TestDerivedScriptComponent : public TestScriptComponent
	{
		REFLECT_BODY();

		uint32 _derivedTickCount{ 0 };

		/** @brief 부모 틱 후 파생 틱 카운트를 더합니다. */
		void onTick( float32 dt ) override
		{
			TestScriptComponent::onTick( dt );
			_derivedTickCount += 2;
		}
	};

	REFLECT()
	struct TestGrandChildScriptComponent : public TestDerivedScriptComponent
	{
		REFLECT_BODY();

		uint32 _grandChildTickCount{ 0 };

		/** @brief 부모 틱 후 손자 틱 카운트를 더합니다. */
		void onTick( float32 dt ) override
		{
			TestDerivedScriptComponent::onTick( dt );
			_grandChildTickCount += 3;
		}
	};
} // namespace sw

// ------------------------------------------------------------------------------
// 5) ENUM Flags — 전역 비트플래그 샘플
// ------------------------------------------------------------------------------
ENUM( Flags )
enum class TestFlag : uint32
{
	None	= 0,
	Read	= 1 << 0,
	Write	= 1 << 1,
	Execute = 1 << 2
};
