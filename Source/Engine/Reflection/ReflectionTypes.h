/**
 * @file ReflectionTypes.h
 * @brief 리플렉션용 프로퍼티 / enum / 함수 / 타입 메타데이터
 */
#pragma once

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Utility/Task/TaskTypes.h"

namespace sw
{

	/// @brief Function Net Role — 목록은 PredefinedFunctionNetRole.xxx
	enum class FunctionNetRole : uint8
	{
#define REGISTER_FUNCTION_NET_ROLE( Name ) Name,
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"

#undef REGISTER_FUNCTION_NET_ROLE
	};

	/// @brief PROPERTY() 저작 메타 (카테고리, 기본값, 범위, XML 속성)
	struct SW_API PropertyMetadata
	{
		string _category;
		string _displayName;
		string _tooltip;
		/**
		 * @brief 에셋이 이 프로퍼티를 생략할 때 쓰는 저작 기본값 (PROPERTY(Default="...")).
		 * @details Xml/Json/Binary deserialize가 적용. C++ 멤버 초기화자와는 별개이므로 맞춰 두세요.
		 */
		string _defaultValue;
		/** @brief 소프트 에셋 힌트 (PROPERTY(AssetPath) / AssetType="Texture"). */
		string	_assetType;
		float32 _minRange;
		float32 _maxRange;
		uint8	_bHasRange : 1;
		uint8	_bReadOnly : 1;
		/** @brief 부모 엘리먼트의 XML attribute로 직렬화 (PROPERTY(XmlAttribute)). */
		uint8 _bXmlAttribute : 1;
		uint8 _bAssetPath	 : 1;
		/** @brief 값이 ReflectAny (또는 type+blob 다형 페이로드). */
		uint8				   _bPolymorphic  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 3;

		/** @brief 범위/플래그 끈 기본값. */
		PropertyMetadata() noexcept;
	};

	/// @brief FUNCTION() 저작 메타 (카테고리, NetRole, static/const)
	struct SW_API FunctionMetadata
	{
		string				   _category;
		string				   _displayName;
		string				   _tooltip;
		FunctionNetRole		   _netRole;
		uint8				   _bReliable	 : 1;
		uint8				   _bValidate	 : 1;
		uint8				   _bConstructor : 1; ///< REFLECT 타입 ctor invoker (objPtr에 placement-new)
		uint8				   _bStatic		 : 1; ///< C++ static member / FUNCTION on static
		uint8				   _bConst		 : 1; ///< const member function
		[[maybe_unused]] uint8 _reserved	 : 3;

		/** @brief 플래그 끈 기본값. */
		FunctionMetadata() noexcept;
	};

	/**
	 * @brief 재귀 컨테이너 스키마 (vector&lt;vector&lt;T&gt;&gt;, map&lt;K,vector&lt;V&gt;&gt;, …).
	 */
	struct NestedContainerInfo
	{
		ContainerKind					_kind = ContainerKind::None;
		hashed_string					_elementTypeName;
		hashed_string					_keyTypeName;
		shared_ptr<IContainerWrapper>	_wrapper;
		shared_ptr<NestedContainerInfo> _elementNested; ///< element/value is itself a container
	};

	/// @brief 리플렉션 프로퍼티: 오프셋, 타입, 별칭, 컨테이너 래퍼
	struct SW_API PropertyInfo
	{
		using PropertyBindingDelegate = Delegate<void( const PropertyInfo& prop, const void* pInstance )>;

		shared_ptr<IContainerWrapper>	_containerWrapper;
		shared_ptr<NestedContainerInfo> _nestedContainer; ///< full nesting chain when container
		mutable PropertyBindingDelegate _onPropertyBoundChanged;

		size_t _offset;

		hashed_string		  _name;
		hashed_string		  _typeName;
		hashed_string		  _elementTypeName;
		hashed_string		  _keyTypeName;
		vector<hashed_string> _listAliases; ///< PROPERTY(Alias=…) 복수 옛 키
		PropertyMetadata	  _metadata;

		mutable uint32 _cachedNameHash;

		ContainerKind		   _containerKind;
		uint8				   _bIsContainer  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;

		/** @brief 오프셋 0, 컨테이너 아님. */
		PropertyInfo() noexcept;

		/** @brief 이름·타입·오프셋으로 프로퍼티를 채웁니다. */
		PropertyInfo( hashed_string name, hashed_string typeName, size_t offset,
					  bool bIsContainer = false, ContainerKind containerKind = ContainerKind::None,
					  hashed_string elementTypeName = {}, hashed_string keyTypeName = {},
					  shared_ptr<IContainerWrapper> containerWrapper = nullptr,
					  hashed_string					alias			 = {} );

		/** @brief 이름 해시를 반환합니다. 없으면 계산해 캐시합니다. */
		uint32 getNameHash() const noexcept
		{
			if ( _cachedNameHash == 0 && _name.empty() == false )
				_cachedNameHash = _name.getHash();
			return _cachedNameHash;
		}

		/** @brief 이름 또는 임의의 Alias 해시와 일치하면 true. */
		bool matchesNameHash( const uint32 nameOrAliasHash ) const noexcept
		{
			if ( getNameHash() == nameOrAliasHash )
				return true;
			for ( const hashed_string& alias : _listAliases )
			{
				if ( alias.empty() == false && alias.getHash() == nameOrAliasHash )
					return true;
			}
			return false;
		}

		/** @brief 이름 또는 Alias와 일치하면 true. */
		bool matchesName( const hashed_string& nameOrAlias ) const
		{
			if ( _name == nameOrAlias )
				return true;
			for ( const hashed_string& alias : _listAliases )
			{
				if ( alias == nameOrAlias )
					return true;
			}
			return false;
		}

		/** @brief 값 변경 콜백을 바인딩합니다. */
		void bindOnChanged( PropertyBindingDelegate delegate ) const { _onPropertyBoundChanged = std::move( delegate ); }

		template <typename T, typename ObjectType>
		/** @brief 인스턴스 프로퍼티 값을 쓰고 옵저버/바인딩을 알립니다. */
		void setValue( ObjectType* pInstance, const T& newValue ) const
		{
			T* pPtr = reinterpret_cast<T*>( reinterpret_cast<utf8*>( pInstance ) + _offset );
			if ( *pPtr == newValue )
				return;

			*pPtr = newValue;
			if constexpr ( std::is_base_of_v<IPropertyObserver, ObjectType> )
			{
				IPropertyObserver* pObserver = static_cast<IPropertyObserver*>( pInstance );
				pObserver->onPropertyChanged( _name );
			}

			if ( _onPropertyBoundChanged.isBound() )
				_onPropertyBoundChanged( *this, pInstance );
		}

		template <typename T>
		/** @brief 인스턴스 + 오프셋의 값 포인터. */
		T* getValuePtr( void* pInstance ) const
		{
			return reinterpret_cast<T*>( reinterpret_cast<utf8*>( pInstance ) + _offset );
		}

		template <typename T>
		/** @brief 인스턴스 + 오프셋의 값 포인터. */
		const T* getValuePtr( const void* pInstance ) const
		{
			return reinterpret_cast<const T*>( reinterpret_cast<const utf8*>( pInstance ) + _offset );
		}
	};

	/// @brief 등록된 enum: 이름↔값, Flags, Invalid/Count 센티널
	struct SW_API EnumInfo
	{
		unordered_map<hashed_string, int64> _mapNameToValue;
		unordered_map<int64, hashed_string> _mapValueToName;
		hashed_string						_name;
		hashed_string						_fullyQualifiedName;
		hashed_string						_moduleName;
		int64								_invalidValue{ 0 };
		int64								_countValue{ 0 };
		uint8								_bIsBitFlag	   : 1;
		uint8								_bHasInvalid   : 1;
		uint8								_bHasCount	   : 1;
		[[maybe_unused]] uint8				_reservedFlags : 5;

		/** @brief 빈 이름↔값 맵. */
		EnumInfo() noexcept;

		/** @brief ENUM(Invalid/Count) 센티널을 반영한 유효성. 메타가 없으면 true. */
		bool isValidValue( int64 value ) const noexcept
		{
			if ( _bHasInvalid != 0 && value == _invalidValue )
				return false;
			if ( _bHasCount != 0 && value >= _countValue )
				return false;
			return true;
		}

		/** @brief 문자열로 변환합니다. */
		hashed_string toString( int64 val ) const
		{
			auto iter = _mapValueToName.find( val );
			return iter != _mapValueToName.end() ? iter->second : hashed_string();
		}

		/** @brief 비트플래그 값을 이름 문자열로 바꿉니다. */
		hashed_string toStringFlags( int64 val ) const
		{
			if ( _bIsBitFlag == false )
				return toString( val );

			if ( val == 0 )
			{
				auto iter = _mapValueToName.find( 0 );
				return iter != _mapValueToName.end() ? iter->second : hashed_string( "None" );
			}

			// _mapValueToName 만 사용 — ValueAlias 가 _mapNameToValue 에 있어도 출력에 중복되지 않음.
			string result;
			for ( const auto& [bitVal, name] : _mapValueToName )
			{
				if ( bitVal != 0 && ( val & bitVal ) == bitVal )
				{
					if ( result.empty() == false )
						result += " | ";
					result += name.c_str();
				}
			}

			return hashed_string( result.c_str() );
		}

		/** @brief `"A | B"` 플래그 문자열을 값으로 파싱합니다. */
		int64 stringFlagsToValue( string_view flagsStr ) const
		{
			if ( _bIsBitFlag == false )
			{
				hashed_string nameKey{ flagsStr };
				auto		  iter = _mapNameToValue.find( nameKey );
				return iter != _mapNameToValue.end() ? iter->second : 0;
			}

			int64			intResult{ 0 };
			string_splitter splitter( flagsStr, { "|" } );
			for ( string_view tokenView : splitter.getSplitList() )
			{
				string_view trimmedStr = StringUtil::trim( tokenView );
				if ( trimmedStr.empty() == false )
				{
					hashed_string tokenKey{ trimmedStr };
					auto		  iter = _mapNameToValue.find( tokenKey );
					if ( iter != _mapNameToValue.end() )
						intResult |= iter->second;
				}
			}

			return intResult;
		}

		/** @brief 값 → interned enumerator 이름. Invalid/Count 센티널이면 nullptr. */
		const utf8* valueToCString( int64 value ) const
		{
			if ( isValidValue( value ) == false )
				return nullptr;
			const hashed_string name = ( _bIsBitFlag != 0 ) ? toStringFlags( value ) : toString( value );
			return name.empty() ? nullptr : name.c_str();
		}

		/** @brief 이름 → 값. 대소문자 무시. 비트플래그는 `A|B` 도 허용. */
		bool tryParse( string_view name, int64& outValue ) const
		{
			if ( name.empty() )
				return false;

			{
				hashed_string key{ name };
				const auto	  it = _mapNameToValue.find( key );
				if ( it != _mapNameToValue.end() )
				{
					if ( isValidValue( it->second ) == false )
						return false;
					outValue = it->second;
					return true;
				}
			}

			for ( const auto& [nameKey, val] : _mapNameToValue )
			{
				if ( StringUtil::equalsIgnoreCase( name, nameKey.view() ) )
				{
					if ( isValidValue( val ) == false )
						return false;
					outValue = val;
					return true;
				}
			}

			if ( _bIsBitFlag != 0 )
			{
				outValue = stringFlagsToValue( name );
				return isValidValue( outValue );
			}
			return false;
		}
	};

	/// @brief 리플렉션 메서드: 이름, 시그니처, invoker
	struct SW_API FunctionInfo
	{
		string										  _name;
		hashed_string								  _hashName;
		string										  _returnTypeName;	   ///< clang spelling (e.g. void, int32)
		vector<string>								  _listParamTypeNames; ///< clang spellings in declaration order
		FunctionMetadata							  _metadata;
		Delegate<TaskValue( void*, const TaskArgs& )> _invoker; ///< instance + args → TaskValue
	};

	/// @brief 등록된 타입: FQN, 프로퍼티/메서드, 생성 가능 여부
	struct SW_API TypeInfo
	{
		hashed_string _name;
		hashed_string _fullyQualifiedName;
		hashed_string _parentFQN;
		hashed_string _moduleName;
		uint32		  _typeId;
		size_t		  _size;
		/** @brief `$ctor`로 placement-new 된 인스턴스를 파괴합니다. 없으면 nullptr. */
		void ( *_destroyInstance )( void* ) = nullptr;
		vector<PropertyInfo>									  _propertyList;
		vector<FunctionInfo>									  _listMethods;
		mutable unordered_map<hashed_string, const PropertyInfo*> _mapNameToProperty;
		mutable unordered_map<hashed_string, const FunctionInfo*> _mapNameToMethod;
		mutable vector<PropertyInfo>							  _propertyListWithBase;
		/** @brief REFLECT(Abstract) / C++ abstract — not constructible (UCLASS(Abstract)). */
		uint8 _bAbstract : 1;
		/** @brief REFLECT(Static) type (function-library). Not the same as FunctionMetadata::_bStatic. */
		uint8 _bStatic : 1;
		/** @brief 내장 스칼라/문자열 (int32, bool, sw::string, …). REFLECT codegen 없음. */
		uint8				   _bPrimitive				   : 1;
		mutable uint8		   _bIsCacheBuilt			   : 1;
		mutable uint8		   _bIsPODFastPath			   : 1;
		mutable uint8		   _bIsPODCalculated		   : 1;
		mutable uint8		   _bPropertyListWithBaseBuilt : 1;
		[[maybe_unused]] uint8 _reservedTypeFlags		   : 1;

		/** @brief 빈 TypeInfo. */
		TypeInfo() noexcept;
		TypeInfo( const TypeInfo& other );
		TypeInfo( TypeInfo&& other ) noexcept;
		TypeInfo& operator=( const TypeInfo& other );
		TypeInfo& operator=( TypeInfo&& other ) noexcept;

		/** @brief 팩토리/$ctor가 인스턴스를 만들 수 있으면 true. */
		bool canConstruct() const noexcept { return _bAbstract == 0 && _bStatic == 0 && _bPrimitive == 0; }

		/** @brief REFLECT 없이 등록된 내장 타입 (int32, float32, …). */
		bool isPrimitive() const noexcept { return _bPrimitive != 0; }

		/** @brief 직렬화/복사 시 memcpy POD 경로를 쓸 수 있으면 true. */
		bool usesPodCopyFastPath() const;
		/** @brief 이 타입이 targetFqn이거나 그 파생이면 true. */
		bool isDerivedFrom( const hashed_string& targetFqn ) const;

		/**
		 * @brief 자신 + 상속 베이스 프로퍼티 (단일 부모 체인).
		 * @details 자식 이름이 부모를 덮어씁니다. 단일 상속에 안전.
		 */
		const vector<PropertyInfo>& getPropertiesWithBase() const;

		/** @brief 이름→프로퍼티/메서드 조회 캐시를 만듭니다. */
		void buildLookupCache() const
		{
			if ( _bIsCacheBuilt != 0 )
				return;

			for ( const PropertyInfo& prop : _propertyList )
			{
				_mapNameToProperty[prop._name] = &prop;
				for ( const hashed_string& alias : prop._listAliases )
				{
					if ( alias.empty() == false )
						_mapNameToProperty[alias] = &prop;
				}
			}

			for ( const FunctionInfo& method : _listMethods )
			{
				_mapNameToMethod[method._hashName] = &method;
			}

			_bIsCacheBuilt = true;
		}

		/** @brief 이름 또는 alias로 프로퍼티를 찾습니다. */
		const PropertyInfo* findProperty( const hashed_string& propNameOrAlias ) const
		{
			if ( _propertyList.size() <= 4 )
			{
				for ( const PropertyInfo& prop : _propertyList )
				{
					if ( prop.matchesName( propNameOrAlias ) )
						return &prop;
				}
				return nullptr;
			}

			buildLookupCache();
			auto it = _mapNameToProperty.find( propNameOrAlias );
			return it != _mapNameToProperty.end() ? it->second : nullptr;
		}

		/** @brief 이름 메서드를 찾습니다. */
		const FunctionInfo* findMethod( const hashed_string& methodName ) const
		{
			if ( _listMethods.size() <= 4 )
			{
				for ( const FunctionInfo& method : _listMethods )
				{
					if ( method._hashName == methodName )
						return &method;
				}
				return nullptr;
			}

			buildLookupCache();
			auto it = _mapNameToMethod.find( methodName );
			return it != _mapNameToMethod.end() ? it->second : nullptr;
		}
	};

} // namespace sw
