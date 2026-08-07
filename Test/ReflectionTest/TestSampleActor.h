#pragma once
/**
 * @file TestSampleActor.h
 * @brief Auto-generated documentation header
 */

#include "Core/Reflection/ReflectionCore.h"

namespace sw
{
	REFLECT()
	struct SampleTestActor
	{
		PROPERTY()
		int32 _hp = 100;

		PROPERTY()
		std::string _name = "Hero";
	};

	REFLECT()
	struct AliasAndReorderTestActor
	{
		PROPERTY( Alias = "hp" )
		int32 _currentHp = 100;

		PROPERTY()
		int32 _score = 50;
	};

	ENUM()
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
				std::string _innerData = "NestedData";

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
} // namespace sw
