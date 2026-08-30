/**
 * @file ReflectionTypes.h
 * @brief 리플렉션용 프로퍼티 / enum / 함수 / 타입 메타데이터
 */
#pragma once
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{

	/// @brief Function Net Role — 목록은 PredefinedFunctionNetRole.xxx
	enum class FunctionNetRole : uint8
	{
#define REGISTER_FUNCTION_NET_ROLE( Name ) Name,
#include "Core/Predefined/PredefinedFunctionNetRole.xxx"

#undef REGISTER_FUNCTION_NET_ROLE
	};

	/// @brief REFLECT() 클래스/구조체 저작 메타
	struct SW_API TypeMetadata
	{
#if !defined( SW_SHIPPING )
		string								 _category;
		string								 _displayName;
		string								 _tooltip;
		unordered_map<hashed_string, string> _mapCustomMeta;
		uint8								 _bHideInMenu	: 1; ///< "Add Component" 메뉴 숨김
		[[maybe_unused]] uint8				 _reservedFlags : 7;
#else
		[[maybe_unused]] uint8 _reservedEmpty;
#endif

		TypeMetadata() noexcept;

		/** @brief 커스텀 메타데이터 태그를 조회합니다. (Shipping 빌드에서는 nullptr) */
		const string* findCustomMeta( const hashed_string& key ) const noexcept
		{
#if !defined( SW_SHIPPING )
			auto iter = _mapCustomMeta.find( key );
			return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
			(void)key;
			return nullptr;
#endif
		}
	};

	/// @brief PROPERTY() 저작 메타 (카테고리, 기본값, 범위, XML 속성)
	struct SW_API PropertyMetadata
	{
#if !defined( SW_SHIPPING )
		string								 _category;
		string								 _displayName;
		string								 _tooltip;
		unordered_map<hashed_string, string> _mapCustomMeta;
#endif
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
		uint8 _bPolymorphic : 1;
		/** @brief 직렬화(Json/Xml/Binary/Diff) 저장/로드 대상에서 제외 (Transient / NonSerialized). */
		uint8 _bTransient : 1;
#if !defined( SW_SHIPPING )
		/** @brief 에디터 인스펙터 패널 UI에서 숨김 (HideInInspector). */
		uint8				   _bHideInInspector : 1;
		[[maybe_unused]] uint8 _reservedFlags	 : 1;
#else
		[[maybe_unused]] uint8 _reservedFlags : 2;
#endif

		/** @brief 범위/플래그 끈 기본값. */
		PropertyMetadata() noexcept;

		/** @brief 커스텀 메타데이터 태그를 조회합니다. (Shipping 빌드에서는 nullptr) */
		const string* findCustomMeta( const hashed_string& key ) const noexcept
		{
#if !defined( SW_SHIPPING )
			auto iter = _mapCustomMeta.find( key );
			return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
			(void)key;
			return nullptr;
#endif
		}
	};

	/// @brief FUNCTION() 저작 메타 (카테고리, NetRole, static/const)
	struct SW_API FunctionMetadata
	{
#if !defined( SW_SHIPPING )
		string								 _category;
		string								 _displayName;
		string								 _tooltip;
		unordered_map<hashed_string, string> _mapCustomMeta;
#endif
		FunctionNetRole _netRole;
		uint8			_bReliable	  : 1;
		uint8			_bValidate	  : 1;
		uint8			_bConstructor : 1; ///< REFLECT 타입 ctor invoker (objPtr에 placement-new)
		uint8			_bStatic	  : 1; ///< C++ static member / FUNCTION on static
		uint8			_bConst		  : 1; ///< const member function
#if !defined( SW_SHIPPING )
		uint8				   _bCallInEditor : 1; ///< 에디터 인스펙터 패널에서 원클릭 실행 버튼 노출
		[[maybe_unused]] uint8 _reserved	  : 2;
#else
		[[maybe_unused]] uint8 _reserved : 3;
#endif

		/** @brief 플래그 끈 기본값. */
		FunctionMetadata() noexcept;

		/** @brief 커스텀 메타데이터 태그를 조회합니다. (Shipping 빌드에서는 nullptr) */
		const string* findCustomMeta( const hashed_string& key ) const noexcept
		{
#if !defined( SW_SHIPPING )
			auto iter = _mapCustomMeta.find( key );
			return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
			(void)key;
			return nullptr;
#endif
		}
	};

	/**
	 * @brief 재귀 컨테이너 스키마 (vector&lt;vector&lt;T&gt;&gt;, map&lt;K,vector&lt;V&gt;&gt;, …).
	 */
	struct NestedContainerInfo
	{
		ContainerKind					_kind = ContainerKind::None;
		hashed_string					_typeName; ///< 컨테이너 TypeInfo 이름 (vector, unordered_map, …)
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
		vector<hashed_string> _listAlias; ///< PROPERTY(Alias=…) 복수 옛 키
		PropertyMetadata	  _metadata;

		mutable uint32 _cachedNameHash;

		uint32				   _bitOffset;
		ContainerKind		   _containerKind;
		uint8				   _bitMask;
		uint8				   _bIsContainer  : 1;
		uint8				   _bIsBitField	  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 6;

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
			if ( _listAlias.empty() )
				return false;
			for ( const hashed_string& alias : _listAlias )
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
			if ( _listAlias.empty() )
				return false;
			for ( const hashed_string& alias : _listAlias )
			{
				if ( alias == nameOrAlias )
					return true;
			}
			return false;
		}

		/** @brief 이 프로퍼티가 직렬화 가능한 컨테이너 래퍼를 가지고 있는지 반환합니다. */
		bool hasContainerWrapper() const noexcept
		{
			return _containerWrapper != nullptr;
		}

		/** @brief 단일 컨테이너든 중첩 컨테이너든 동일하게 처리하기 위한 shape 정보를 반환합니다. */
		NestedContainerInfo getContainerShape() const;

		/** @brief 값 변경 콜백을 바인딩합니다. */
		void bindOnChanged( PropertyBindingDelegate delegate ) const { _onPropertyBoundChanged = std::move( delegate ); }

		template <typename T, typename ObjectType>
		/** @brief 인스턴스 프로퍼티 값을 읽습니다. (비트필드 지원) */
		T getValue( const ObjectType* pInstance ) const
		{
			if ( _bIsBitField == SW_TRUE )
			{
				const uint8* pByte = reinterpret_cast<const uint8*>( pInstance ) + _offset;
				const bool	 bVal  = ( ( *pByte & _bitMask ) != 0 );
				if constexpr ( std::is_same_v<T, bool> )
				{
					return bVal;
				}
				else
				{
					return static_cast<T>( bVal ? 1 : 0 );
				}
			}

			const T* pPtr = reinterpret_cast<const T*>( reinterpret_cast<const utf8*>( pInstance ) + _offset );
			return *pPtr;
		}

		template <typename T, typename ObjectType>
		/** @brief 인스턴스 프로퍼티 값을 쓰고 옵저버/바인딩을 알립니다. (비트필드 지원) */
		void setValue( ObjectType* pInstance, const T& newValue ) const
		{
			if ( _bIsBitField == SW_TRUE )
			{
				uint8* pByte = reinterpret_cast<uint8*>( pInstance ) + _offset;
				bool   bVal	 = false;
				if constexpr ( std::is_same_v<T, bool> )
					bVal = newValue;
				else if constexpr ( std::is_same_v<T, float32> )
					bVal = ( MathUtil::nearEqual( newValue, 0.0f ) == false );
				else if constexpr ( std::is_same_v<T, float64> )
					bVal = ( MathUtil::nearEqual( newValue, 0.0 ) == false );
				else
					bVal = ( newValue != static_cast<T>( 0 ) );

				if ( bVal )
					*pByte |= _bitMask;
				else
					*pByte &= static_cast<uint8>( ~_bitMask );

				if constexpr ( std::is_base_of_v<IPropertyObserver, ObjectType> )
				{
					IPropertyObserver* pObserver = static_cast<IPropertyObserver*>( pInstance );
					pObserver->onPropertyChanged( _name );
				}

				if ( _onPropertyBoundChanged.isBound() )
					_onPropertyBoundChanged( *this, pInstance );
				return;
			}

			T* pPtr = reinterpret_cast<T*>( reinterpret_cast<utf8*>( pInstance ) + _offset );
			if constexpr ( std::is_same_v<T, float32> || std::is_same_v<T, float64> )
			{
				if ( MathUtil::nearEqual( *pPtr, newValue ) )
					return;
			}
			else if ( *pPtr == newValue )
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
		/** @brief 인스턴스 + 오프셋의 값 포인터. (비트필드는 nullptr 반환) */
		T* getValuePtr( void* pInstance ) const
		{
			if ( _bIsBitField == SW_TRUE )
				return nullptr;
			return reinterpret_cast<T*>( reinterpret_cast<utf8*>( pInstance ) + _offset );
		}

		template <typename T>
		/** @brief 인스턴스 + 오프셋의 값 포인터. (비트필드는 nullptr 반환) */
		const T* getValuePtr( const void* pInstance ) const
		{
			if ( _bIsBitField == SW_TRUE )
				return nullptr;
			return reinterpret_cast<const T*>( reinterpret_cast<const utf8*>( pInstance ) + _offset );
		}

		/** @brief 인스턴스 기준 프로퍼티의 원시 메모리 시작 포인터를 반환합니다. */
		void* getRawPtr( void* pInstance ) const noexcept
		{
			return reinterpret_cast<utf8*>( pInstance ) + _offset;
		}

		/** @brief 인스턴스 기준 프로퍼티의 원시 메모리 const 시작 포인터를 반환합니다. */
		const void* getRawPtr( const void* pInstance ) const noexcept
		{
			return reinterpret_cast<const utf8*>( pInstance ) + _offset;
		}

		/** @brief 커스텀 메타데이터 태그를 조회합니다. */
		const string* findCustomMeta( const hashed_string& key ) const noexcept
		{
			return _metadata.findCustomMeta( key );
		}
	};

	/// @brief 등록된 enum: 이름↔값, Flags, Invalid/Count 센티널
	struct SW_API EnumInfo
	{
		unordered_map<hashed_string, int64> _mapNameToValue;
		unordered_map<int64, hashed_string> _mapValueToName;
#if !defined( SW_SHIPPING )
		unordered_map<hashed_string, string> _mapCustomMeta;
#endif
		hashed_string		   _name;
		hashed_string		   _fullyQualifiedName;
		hashed_string		   _moduleName;
		int64				   _invalidValue{ 0 };
		int64				   _countValue{ 0 };
		uint8				   _bIsBitFlag	  : 1;
		uint8				   _bHasInvalid	  : 1;
		uint8				   _bHasCount	  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 5;

		/** @brief 빈 이름↔값 맵. */
		EnumInfo() noexcept;

		/** @brief 커스텀 메타데이터 태그를 조회합니다. (Shipping 빌드에서는 nullptr) */
		const string* findCustomMeta( const hashed_string& key ) const noexcept
		{
#if !defined( SW_SHIPPING )
			auto iter = _mapCustomMeta.find( key );
			return ( iter != _mapCustomMeta.end() ) ? &iter->second : nullptr;
#else
			(void)key;
			return nullptr;
#endif
		}

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
			if ( _bIsBitFlag == SW_FALSE )
				return toString( val );

			if ( val == 0 )
			{
				auto iter = _mapValueToName.find( 0 );
				return iter != _mapValueToName.end() ? iter->second : hashed_string( constants::reflection::kNone );
			}

			// _mapValueToName 만 사용 — ValueAlias 가 _mapNameToValue 에 있어도 출력에 중복되지 않음.
			string result;
			result.reserve( 64 );
			for ( const auto& [bitVal, name] : _mapValueToName )
			{
				if ( bitVal != 0 && ( val & bitVal ) == bitVal )
				{
					if ( result.empty() == false )
						result += constants::reflection::kFlagSeparator;
					result += name.c_str();
				}
			}

			return hashed_string( result.c_str() );
		}

		/** @brief `"A | B"` 플래그 문자열을 값으로 파싱합니다. */
		int64 stringFlagsToValue( string_view flagsStr ) const
		{
			if ( _bIsBitFlag == SW_FALSE )
			{
				hashed_string nameKey{ flagsStr };
				auto		  iter = _mapNameToValue.find( nameKey );
				return iter != _mapNameToValue.end() ? iter->second : 0;
			}

			int64  intResult{ 0 };
			size_t startPos{ 0 };
			while ( startPos < flagsStr.size() )
			{
				const size_t	  delimiterPos = flagsStr.find( '|', startPos );
				const size_t	  endPos	   = ( delimiterPos != string_view::npos ) ? delimiterPos : flagsStr.size();
				const string_view token		   = StringUtil::trim( flagsStr.substr( startPos, endPos - startPos ) );
				if ( token.empty() == false )
				{
					hashed_string tokenKey{ token };
					auto		  iter = _mapNameToValue.find( tokenKey );
					if ( iter != _mapNameToValue.end() )
						intResult |= iter->second;
				}
				if ( delimiterPos == string_view::npos )
					break;
				startPos = delimiterPos + 1;
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

			for ( const auto& [nameKey, enumValue] : _mapNameToValue )
			{
				if ( StringUtil::equals( name, nameKey.view(), true ) )
				{
					if ( isValidValue( enumValue ) == false )
						return false;
					outValue = enumValue;
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
		string										  _returnTypeName;		  ///< clang spelling (e.g. void, int32)
		vector<string>								  _listParameterTypeName; ///< clang spellings in declaration order
		FunctionMetadata							  _metadata;
		Delegate<TaskValue( void*, const TaskArgs& )> _invoker; ///< instance + args → TaskValue

		/** @brief 커스텀 메타데이터 태그를 조회합니다. */
		const string* findCustomMeta( const hashed_string& key ) const noexcept
		{
			return _metadata.findCustomMeta( key );
		}
	};

	/// @brief 등록된 타입: FQN, 프로퍼티/메서드, 생성 가능 여부
	struct SW_API TypeInfo
	{
		size_t _size;
		/** @brief `$ctor`로 placement-new 된 인스턴스를 파괴합니다. 없으면 nullptr. */
		void ( *_destroyInstance )( void* ) = nullptr;
		hashed_string											  _name;
		hashed_string											  _fullyQualifiedName;
		hashed_string											  _parentFQN;
		hashed_string											  _moduleName;
		vector<PropertyInfo>									  _listProperty;
		vector<FunctionInfo>									  _listMethod;
		TypeMetadata											  _metadata;
		mutable vector<PropertyInfo>							  _propertyListWithBase;
		mutable unordered_map<hashed_string, const PropertyInfo*> _mapNameToProperty;
		mutable unordered_map<hashed_string, const FunctionInfo*> _mapNameToMethod;
		uint32													  _typeId;
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
		[[maybe_unused]] uint8 _reservedPadding[3];

		/** @brief 빈 TypeInfo. */
		TypeInfo() noexcept;
		~TypeInfo() = default;
		TypeInfo( const TypeInfo& other );
		TypeInfo( TypeInfo&& other ) noexcept;
		TypeInfo& operator=( const TypeInfo& other );
		TypeInfo& operator=( TypeInfo&& other ) noexcept;

		/** @brief 커스텀 메타데이터 태그를 조회합니다. */
		const string* findCustomMeta( const hashed_string& key ) const noexcept
		{
			return _metadata.findCustomMeta( key );
		}

		/** @brief 카테고리를 반환합니다. */
		const string& getCategory() const noexcept
		{
#if !defined( SW_SHIPPING )
			return _metadata._category;
#else
			static const string s_empty;
			return s_empty;
#endif
		}

		/** @brief 표시 이름을 반환합니다. DisplayName 메타가 없으면 C++ 타입 이름을 반환합니다. */
		const utf8* getDisplayName() const noexcept
		{
#if !defined( SW_SHIPPING )
			if ( _metadata._displayName.empty() == false )
				return _metadata._displayName.c_str();
#endif
			return _name.c_str();
		}

		/** @brief 툴팁 문자열을 반환합니다. */
		const string& getTooltip() const noexcept
		{
#if !defined( SW_SHIPPING )
			return _metadata._tooltip;
#else
			static const string s_empty;
			return s_empty;
#endif
		}

		/** @brief "Add Component" 메뉴에서 숨겨야 하는지 여부를 반환합니다. */
		bool isHiddenInMenu() const noexcept
		{
#if !defined( SW_SHIPPING )
			return _metadata._bHideInMenu != 0;
#else
			return false;
#endif
		}

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

			_mapNameToProperty.reserve( _listProperty.size() * 2 );
			_mapNameToMethod.reserve( _listMethod.size() );

			for ( const PropertyInfo& propertyInfo : _listProperty )
			{
				_mapNameToProperty[propertyInfo._name] = &propertyInfo;
				for ( const hashed_string& alias : propertyInfo._listAlias )
				{
					if ( alias.empty() == false )
						_mapNameToProperty[alias] = &propertyInfo;
				}
			}

			for ( const FunctionInfo& method : _listMethod )
			{
				_mapNameToMethod[method._hashName] = &method;
			}

			_bIsCacheBuilt = SW_TRUE;
		}

		/** @brief 이름 또는 alias로 프로퍼티를 찾습니다. */
		const PropertyInfo* findProperty( const hashed_string& propertyNameOrAlias ) const
		{
			if ( _listProperty.size() <= constants::reflection::kLinearSearchThreshold )
			{
				for ( const PropertyInfo& propertyInfo : _listProperty )
				{
					if ( propertyInfo.matchesName( propertyNameOrAlias ) )
						return &propertyInfo;
				}
				return nullptr;
			}

			buildLookupCache();
			auto it = _mapNameToProperty.find( propertyNameOrAlias );
			return it != _mapNameToProperty.end() ? it->second : nullptr;
		}

		/** @brief 이름 메서드를 찾습니다. */
		const FunctionInfo* findMethod( const hashed_string& methodName ) const
		{
			if ( _listMethod.size() <= constants::reflection::kLinearSearchThreshold )
			{
				for ( const FunctionInfo& method : _listMethod )
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

		/** @brief 현재 클래스 및 부모 상속 체인에서 프로퍼티를 검색합니다 (평탄화 캐시 미사용 제로 할당 검색). */
		const PropertyInfo* findPropertyInHierarchy( const hashed_string& propertyNameOrAlias ) const;

		template <typename Func>
		/** @brief 프로퍼티를 순회합니다. bIncludeBase가 true이면 상속 체인 포함. */
		void forEachProperty( Func&& func, bool bIncludeBase = false ) const
		{
			if ( bIncludeBase == false )
			{
				for ( const PropertyInfo& propertyInfo : _listProperty )
				{
					func( propertyInfo );
				}
			}
			else
			{
				for ( const PropertyInfo& propertyInfo : getPropertiesWithBase() )
				{
					func( propertyInfo );
				}
			}
		}

		template <typename Func>
		/** @brief 메서드를 순회합니다. */
		void forEachMethod( Func&& func ) const
		{
			for ( const FunctionInfo& method : _listMethod )
			{
				func( method );
			}
		}
	};

} // namespace sw
