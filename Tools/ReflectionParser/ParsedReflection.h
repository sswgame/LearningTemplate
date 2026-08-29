/**
 * @file ParsedReflection.h
 * @brief AstVisitor가 수집하고 CodeGenerator가 emit하는 파싱 결과 DTO
 * @details 초심자: “헤더에서 뭘 뽑았는지”만 보려면 이 파일부터 보면 됩니다.
 *          AST 순회 로직은 AstVisitor, 코드 출력은 CodeGenerator 쪽입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "ReflectionParser/ParserDefines.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) parse — 컨테이너 / 프로퍼티 / 함수 / 타입 / 열거형
	// ------------------------------------------------------------------------------
	/** @brief Sequence/Map 등 중첩 컨테이너 트리 노드 */
	struct ParsedContainerNode
	{
		string							_containerType; ///< VectorWrapper stem
		string							_typeName;		///< TypeInfo 이름 (vector, unordered_map, …)
		string							_elementTypeName;
		string							_keyTypeName;
		shared_ptr<ParsedContainerNode> _elementNested;
		ContainerKind					_containerKind;
		uint8							_bIsContainer : 1;
		[[maybe_unused]] uint8			_reserved	  : 7;
		[[maybe_unused]] uint16			_padding;

		ParsedContainerNode() noexcept
			: _containerType{}
			, _typeName{}
			, _elementTypeName{}
			, _keyTypeName{}
			, _elementNested{ nullptr }
			, _containerKind{ ContainerKind::None }
			, _bIsContainer{ SW_FALSE }
			, _reserved{ 0 }
			, _padding{ 0 }
		{
		}
	};

	/** @brief PROPERTY(...) 가 붙은 멤버 필드 */
	struct ParsedPropertyInfo
	{
		string							  _name;
		string							  _typeName;
		vector<string>					  _listAlias;
		string							  _category;
		string							  _displayName;
		string							  _tooltip;
		string							  _defaultValue;
		string							  _assetType;
		vector<std::pair<string, string>> _listCustomMeta;
		string							  _containerType;
		string							  _elementTypeName;
		string							  _keyTypeName;
		shared_ptr<ParsedContainerNode>	  _containerTree;
		float32							  _minRange;
		float32							  _maxRange;
		uint32							  _bitOffset;
		uint32							  _byteOffset;
		ContainerKind					  _containerKind;
		uint8							  _bitMask;
		uint8							  _bIsBitField		: 1;
		uint8							  _bReadOnly		: 1;
		uint8							  _bXmlAttribute	: 1;
		uint8							  _bAssetPath		: 1;
		uint8							  _bPolymorphic		: 1;
		uint8							  _bHasRange		: 1;
		uint8							  _bIsContainer		: 1;
		uint8							  _bTransient		: 1;
		uint8							  _bHideInInspector : 1;
		[[maybe_unused]] uint8			  _reserved			: 7;
		[[maybe_unused]] uint16			  _padding;

		ParsedPropertyInfo() noexcept
			: _name{}
			, _typeName{}
			, _listAlias{}
			, _category{}
			, _displayName{}
			, _tooltip{}
			, _defaultValue{}
			, _assetType{}
			, _listCustomMeta{}
			, _containerType{}
			, _elementTypeName{}
			, _keyTypeName{}
			, _containerTree{ nullptr }
			, _minRange{ 0.0f }
			, _maxRange{ 1.0f }
			, _bitOffset{ 0 }
			, _byteOffset{ 0 }
			, _containerKind{ ContainerKind::None }
			, _bitMask{ 0xFF }
			, _bIsBitField{ SW_FALSE }
			, _bReadOnly{ SW_FALSE }
			, _bXmlAttribute{ SW_FALSE }
			, _bAssetPath{ SW_FALSE }
			, _bPolymorphic{ SW_FALSE }
			, _bHasRange{ SW_FALSE }
			, _bIsContainer{ SW_FALSE }
			, _bTransient{ SW_FALSE }
			, _bHideInInspector{ SW_FALSE }
			, _reserved{ 0 }
			, _padding{ 0 }
		{
		}
	};

	/** @brief FUNCTION(...) 가 붙은 메서드(또는 자동 등록 생성자) */
	struct ParsedFunctionInfo
	{
		string							  _name;
		string							  _returnTypeName;
		vector<string>					  _listParameterTypeName;
		string							  _category;
		string							  _displayName;
		string							  _tooltip;
		vector<std::pair<string, string>> _listCustomMeta;
		FunctionNetRole					  _netRole;
		uint8							  _bReliable	 : 1;
		uint8							  _bValidate	 : 1;
		uint8							  _bConstructor	 : 1;
		uint8							  _bStatic		 : 1;
		uint8							  _bConst		 : 1;
		uint8							  _bCallInEditor : 1;
		[[maybe_unused]] uint8			  _reserved		 : 2;
		[[maybe_unused]] uint16			  _padding;

		ParsedFunctionInfo() noexcept
			: _name{}
			, _returnTypeName{}
			, _listParameterTypeName{}
			, _category{ annotationConstants::kDefaultMethodCategory }
			, _displayName{}
			, _tooltip{}
			, _listCustomMeta{}
			, _netRole{ FunctionNetRole::Local }
			, _bReliable{ SW_FALSE }
			, _bValidate{ SW_FALSE }
			, _bConstructor{ SW_FALSE }
			, _bStatic{ SW_FALSE }
			, _bConst{ SW_FALSE }
			, _bCallInEditor{ SW_FALSE }
			, _reserved{ 0 }
			, _padding{ 0 }
		{
		}
	};

	/** @brief REFLECT 가 붙은 클래스·구조체 */
	struct ParsedTypeInfo
	{
		string							  _name;
		string							  _fullyQualifiedName;
		string							  _parentFQN;
		string							  _category;
		string							  _displayName;
		string							  _tooltip;
		vector<string>					  _listAlias;
		vector<std::pair<string, string>> _listCustomMeta;
		vector<ParsedPropertyInfo>		  _listProperty;
		vector<ParsedFunctionInfo>		  _listMethod;
		uint8							  _bAbstract		 : 1;
		uint8							  _bStatic			 : 1;
		uint8							  _bReflectBody		 : 1;
		uint8							  _bComponentFactory : 1;
		uint8							  _bHideInMenu		 : 1;
		[[maybe_unused]] uint8			  _reserved			 : 3;

		ParsedTypeInfo() noexcept
			: _name{}
			, _fullyQualifiedName{}
			, _parentFQN{}
			, _category{}
			, _displayName{}
			, _tooltip{}
			, _listAlias{}
			, _listCustomMeta{}
			, _listProperty{}
			, _listMethod{}
			, _bAbstract{ SW_FALSE }
			, _bStatic{ SW_FALSE }
			, _bReflectBody{ SW_FALSE }
			, _bComponentFactory{ SW_FALSE }
			, _bHideInMenu{ SW_FALSE }
			, _reserved{ 0 }
		{
		}

		bool wantsTypeApi() const noexcept { return _bReflectBody == SW_TRUE; }
		bool wantsComponentFactory() const noexcept { return _bComponentFactory == SW_TRUE; }
	};

	/** @brief 열거형 안의 개별 enumerator */
	struct ParsedEnumeratorInfo
	{
		string _name;
		int64  _value{ 0 };
	};

	/** @brief ENUM(...) 가 붙은 열거형 */
	struct ParsedEnumInfo
	{
		string							  _name;
		string							  _fullyQualifiedName;
		vector<string>					  _listAlias;
		vector<std::pair<string, string>> _listValueAlias;
		vector<std::pair<string, string>> _listCustomMeta;
		vector<ParsedEnumeratorInfo>	  _listEnumerator;
		string							  _invalidEnumerator;
		string							  _countEnumerator;
		uint8							  _bIsBitFlag	: 1;
		uint8							  _bEmitFlagOps : 1;
		[[maybe_unused]] uint8			  _reserved		: 6;

		ParsedEnumInfo() noexcept
			: _name{}
			, _fullyQualifiedName{}
			, _listAlias{}
			, _listValueAlias{}
			, _listCustomMeta{}
			, _listEnumerator{}
			, _invalidEnumerator{}
			, _countEnumerator{}
			, _bIsBitFlag{ SW_FALSE }
			, _bEmitFlagOps{ SW_FALSE }
			, _reserved{ 0 }
		{
		}
	};
} // namespace sw
