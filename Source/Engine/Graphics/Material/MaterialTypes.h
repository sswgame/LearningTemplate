/**
 * @file MaterialTypes.h
 * @brief Material / MaterialInstance 공용 타입·서술체.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	/// @brief 머티리얼 Property 타입 종류를 정의하는 열거형입니다.
	enum class MaterialPropertyType : uint8
	{
		Float,
		Float2,
		Float3,
		Float4,
		Float4x4,
		Uint,
		Uint2,
		Uint3,
		Uint4,
		Int,
		Int2,
		Int3,
		Int4,
		Bool,
		Range,
		Color,
		Enum,
		BitFlag,
		ChannelMask,
		Texture2D,
		TextureCube,
		Texture3D,
		Texture2DArray,
		Keyword,
		Unknown
	};

	/// @brief 머티리얼 Quality Level 종류를 정의하는 열거형입니다.
	enum class MaterialQualityLevel : uint8
	{
		Low	   = 0,
		Medium = 1,
		High   = 2,
		Epic   = 3,
		Count
	};

	/// @brief 머티리얼 Usage Flags 종류를 정의하는 열거형입니다.
	enum class MaterialUsageFlags : uint32
	{
		None		  = 0,
		StaticMesh	  = 1u << 0,
		SkeletalMesh  = 1u << 1,
		Instanced	  = 1u << 2,
		Particles	  = 1u << 3,
		Decal		  = 1u << 4,
		UI			  = 1u << 5,
		PostProcess	  = 1u << 6,
		LightFunction = 1u << 7,
		MorphTargets  = 1u << 8,
		SplineMesh	  = 1u << 9,
	};

	/** @brief 연산자입니다. */
	inline MaterialUsageFlags operator|( MaterialUsageFlags a, MaterialUsageFlags b )
	{
		return static_cast<MaterialUsageFlags>( static_cast<uint32>( a ) | static_cast<uint32>( b ) );
	}
	/** @brief 연산자입니다. */
	inline MaterialUsageFlags operator&( MaterialUsageFlags a, MaterialUsageFlags b )
	{
		return static_cast<MaterialUsageFlags>( static_cast<uint32>( a ) & static_cast<uint32>( b ) );
	}
	/** @brief 플래그 비트가 켜져 있으면 true. */
	inline bool hasFlag( MaterialUsageFlags mask, MaterialUsageFlags flag )
	{
		return ( static_cast<uint32>( mask ) & static_cast<uint32>( flag ) ) != 0;
	}

	/// @brief 머티리얼 enum 항목 (이름 + 값)
	struct MaterialEnumEntry
	{
		string _name;
		uint32 _value{ 0 };
	};

	/// @brief 머티리얼 프로퍼티: 타입, 기본값, 패킹 오프셋
	struct MaterialProperty
	{
		string				 _name;
		MaterialPropertyType _type;
		MaterialPropertyType _shaderType;

		/** @brief 작성 기본값 (XML `_defaultValue`. 로드 시 `_value`로 폴백). */
		string _defaultValue;
		/** @brief 현재 패킹 값 (처음은 `_defaultValue`). */
		string _value;

		string					  _assetPath;
		string					  _enumType;
		vector<MaterialEnumEntry> _listEnumEntries;

		string _displayName;
		string _group;
		string _tooltip;
		string _shaderKeyword;

		float32			   _min;
		float32			   _max;
		uint32			   _offset;
		uint32			   _size;
		RHIDescriptorIndex _textureIndex;

		uint8 _bHdr		 : 1;
		uint8 _bSrgb	 : 1;
		uint8 _bHidden	 : 1;
		uint8 _bAdvanced : 1;
		uint8 _reserved	 : 4;

		/** @brief 플래그/범위 끈 기본값. */
		MaterialProperty() noexcept;
	};

	/// @brief 정적 스위치 (켜면 셰이더 define)
	struct MaterialStaticSwitch
	{
		string _name;
		string _keyword;
		string _keywordOff;

		uint8 _bEnabled		  : 1;
		uint8 _bShaderFeature : 1;
		uint8 _reserved		  : 6;

		/** @brief 꺼진 스위치 기본값. */
		MaterialStaticSwitch() noexcept;
	};

	/// @brief 멀티 컴파일 키워드 목록
	struct MaterialMultiCompile
	{
		string		   _name;
		string		   _selected;
		vector<string> _listOptions;
	};

	/// @brief 스위치 + 멀티컴파일 + 품질 permutation
	struct MaterialPermutationDesc
	{
		MaterialQualityLevel		 _quality	= MaterialQualityLevel::High;
		uint32						 _shaderLOD = 300;
		MaterialUsageFlags			 _usage		= MaterialUsageFlags::StaticMesh;
		vector<string>				 _listAlwaysDefines;
		vector<MaterialStaticSwitch> _listStaticSwitches;
		vector<MaterialMultiCompile> _listMultiCompiles;
	};

	/// @brief 머티리얼 에셋 서술 (셰이더 경로, 프로퍼티, permutation)
	struct MaterialDesc
	{
		/** @brief .material 리소스에서 채움 (팩별 C++ 기본값 없음). */
		string					 _name;
		string					 _shaderPath;
		string					 _blendMode;
		vector<MaterialProperty> _listProperties;
		MaterialPermutationDesc	 _permutations;
	};

	/** @brief 직렬화되는 인스턴스 오버라이드 (게임 MIC / MaterialPropertyBlock). */
	struct MaterialInstanceDesc
	{
		string _name;
		string _parentPath; ///< Optional hint; runtime still needs Material* parent

		/// @brief 인스턴스가 덮어쓴 프로퍼티 값
		struct Override
		{
			string _name;
			string _value;
			string _assetPath; ///< Texture override path (optional)
		};

		vector<Override> _listOverrides;

		/// @brief 인스턴스가 덮어쓴 정적 스위치
		struct KeywordOverride
		{
			string _name;
			bool   _bEnabled{ true };
		};

		vector<KeywordOverride> _listKeywords;

		/// @brief 인스턴스가 고른 멀티컴파일 옵션
		struct MultiCompileOverride
		{
			string _name;
			string _selected;
		};

		vector<MultiCompileOverride> _listMultiCompiles;

		string _quality; ///< empty = inherit
	};

	/// @brief 런타임 머티리얼 데이터 (패킹 버퍼 + 디스크립터)
	struct MaterialData
	{
		vector<MaterialProperty> _listProperties;
		vector<uint8>			 _listBuffer;
	};
} // namespace sw
