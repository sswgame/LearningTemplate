#pragma once
/**
 * @file ReflectionCore.h
 * @brief Auto-generated documentation header
 */

#include "Core/CoreMinimal.h"
#include "Core/Common/CoreServices.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Utility/String/string_splitter.h"
#include "Core/Utility/Task/TaskTypes.h"

#if defined( __REFLECT_PARSER__ )
	#define REFLECT( ... )	__attribute__( ( annotate( "REFLECT;" #__VA_ARGS__ ) ) )
	#define PROPERTY( ... ) __attribute__( ( annotate( "PROPERTY;" #__VA_ARGS__ ) ) )
	#define FUNCTION( ... ) __attribute__( ( annotate( "FUNCTION;" #__VA_ARGS__ ) ) )
	#define ENUM( ... )		__attribute__( ( annotate( "ENUM;" #__VA_ARGS__ ) ) )
#else

	#define REFLECT( ... )
	#define PROPERTY( ... )
	#define FUNCTION( ... )
	#define ENUM( ... )
#endif

namespace sw
{

	class IPropertyObserver
	{
	public:
		virtual ~IPropertyObserver()										= default;
		/**
		 * @brief onPropertyChanged 처리를 수행합니다.
		 */
		virtual void onPropertyChanged( const hashed_string& propertyName ) = 0;
	};

	enum class ContainerKind
	{
		None,
		Sequence,
		Map
	};

	struct ISequenceContainerWrapper;
	struct IMapContainerWrapper;

	struct IContainerWrapper
	{
		virtual ~IContainerWrapper()									= default;
		/**
		 * @brief getKind 처리를 수행합니다.
		 */
		virtual ContainerKind getKind() const							= 0;
		/**
		 * @brief getSize 처리를 수행합니다.
		 */
		virtual size_t		  getSize( const void* containerPtr ) const = 0;
		/**
		 * @brief clear 처리를 수행합니다.
		 */
		virtual void		  clear( void* containerPtr ) const			= 0;

		virtual ISequenceContainerWrapper* asSequence() { return nullptr; }
		virtual IMapContainerWrapper*	   asMap() { return nullptr; }
	};

	struct ISequenceContainerWrapper : public IContainerWrapper
	{
		ContainerKind			   getKind() const override { return ContainerKind::Sequence; }
		ISequenceContainerWrapper* asSequence() override { return this; }
		/**
		 * @brief getElement 처리를 수행합니다.
		 */
		virtual void*			   getElement( void* containerPtr, size_t index ) const			   = 0;
		virtual const void*		   getElementConst( const void* containerPtr, size_t index ) const = 0;
		/**
		 * @brief addElementDefault 처리를 수행합니다.
		 */
		virtual void			   addElementDefault( void* containerPtr ) const				   = 0;
		virtual void			   reserve( void* , size_t  ) const {}
	};

	using MapForEachDelegate = Delegate<void( const void* keyPtr, const void* valPtr )>;

	struct IMapContainerWrapper : public IContainerWrapper
	{
		ContainerKind		  getKind() const override { return ContainerKind::Map; }
		IMapContainerWrapper* asMap() override { return this; }

		/**
		 * @brief forEach 처리를 수행합니다.
		 */
		virtual void forEach( const void* containerPtr, const MapForEachDelegate& callback ) const		= 0;
		/**
		 * @brief insertKeyValue 처리를 수행합니다.
		 */
		virtual void insertKeyValue( void* containerPtr, const void* keyPtr, const void* valPtr ) const = 0;

		/**
		 * @brief getKeySize 처리를 수행합니다.
		 */
		virtual size_t getKeySize() const						= 0;
		/**
		 * @brief getValueSize 처리를 수행합니다.
		 */
		virtual size_t getValueSize() const						= 0;
		/**
		 * @brief defaultConstructKey 처리를 수행합니다.
		 */
		virtual void   defaultConstructKey( void* ptr ) const	= 0;
		/**
		 * @brief defaultConstructValue 처리를 수행합니다.
		 */
		virtual void   defaultConstructValue( void* ptr ) const = 0;
		/**
		 * @brief destroyKey 처리를 수행합니다.
		 */
		virtual void   destroyKey( void* ptr ) const			= 0;
		/**
		 * @brief destroyValue 처리를 수행합니다.
		 */
		virtual void   destroyValue( void* ptr ) const			= 0;
	};

	template <typename TContainer>
	struct VectorWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			return &( ( *container )[index] );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			return &( ( *container )[index] );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->emplace_back( ElementType{} );
		}
		void reserve( void* containerPtr, size_t capacity ) const override
		{
			static_cast<TContainer*>( containerPtr )->reserve( capacity );
		}
	};

	template <typename TContainer>
	struct ListWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			auto  it		= container->begin();
			std::advance( it, index );
			return &( *it );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			auto  it		= container->begin();
			std::advance( it, index );
			return &( *it );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->emplace_back( ElementType{} );
		}
	};

	template <typename TContainer>
	struct DequeWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			return &( ( *container )[index] );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			return &( ( *container )[index] );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->emplace_back( ElementType{} );
		}
	};

	template <typename TContainer>
	struct SetWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			auto  it		= container->begin();
			std::advance( it, index );
			return const_cast<void*>( static_cast<const void*>( &( *it ) ) );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			auto  it		= container->begin();
			std::advance( it, index );
			return &( *it );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->insert( ElementType{} );
		}
	};

	template <typename TContainer>
	struct UnorderedSetWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			auto  it		= container->begin();
			std::advance( it, index );
			return const_cast<void*>( static_cast<const void*>( &( *it ) ) );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			auto  it		= container->begin();
			std::advance( it, index );
			return &( *it );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->insert( ElementType{} );
		}
	};

	template <typename TContainer>
	struct ArrayWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			return &( ( *container )[index] );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			return &( ( *container )[index] );
		}
		void clear( void*  ) const override
		{

		}
		void addElementDefault( void*  ) const override
		{

		}
	};

	template <typename TContainer>
	struct MapWrapper : public IMapContainerWrapper
	{
		using KeyType	= typename TContainer::key_type;
		using ValueType = typename TContainer::mapped_type;

		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void forEach( const void* containerPtr, const MapForEachDelegate& callback ) const override
		{
			const auto* container = static_cast<const TContainer*>( containerPtr );
			for ( const auto& pair : *container )
				callback( &pair.first, &pair.second );
		}
		void insertKeyValue( void* containerPtr, const void* keyPtr, const void* valPtr ) const override
		{
			auto* container = static_cast<TContainer*>( containerPtr );
			( *container )[*static_cast<const KeyType*>( keyPtr )] =
				*static_cast<const ValueType*>( valPtr );
		}
		size_t getKeySize() const override { return sizeof( KeyType ); }
		size_t getValueSize() const override { return sizeof( ValueType ); }
		void   defaultConstructKey( void* ptr ) const override { new ( ptr ) KeyType{}; }
		void   defaultConstructValue( void* ptr ) const override { new ( ptr ) ValueType{}; }
		void   destroyKey( void* ptr ) const override { static_cast<KeyType*>( ptr )->~KeyType(); }
		void   destroyValue( void* ptr ) const override { static_cast<ValueType*>( ptr )->~ValueType(); }
	};

	template <typename TContainer>
	struct UnorderedMapWrapper : public IMapContainerWrapper
	{
		using KeyType	= typename TContainer::key_type;
		using ValueType = typename TContainer::mapped_type;

		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void forEach( const void* containerPtr, const MapForEachDelegate& callback ) const override
		{
			const auto* container = static_cast<const TContainer*>( containerPtr );
			for ( const auto& pair : *container )
				callback( &pair.first, &pair.second );
		}
		void insertKeyValue( void* containerPtr, const void* keyPtr, const void* valPtr ) const override
		{
			auto* container = static_cast<TContainer*>( containerPtr );
			( *container )[*static_cast<const KeyType*>( keyPtr )] =
				*static_cast<const ValueType*>( valPtr );
		}
		size_t getKeySize() const override { return sizeof( KeyType ); }
		size_t getValueSize() const override { return sizeof( ValueType ); }
		void   defaultConstructKey( void* ptr ) const override { new ( ptr ) KeyType{}; }
		void   defaultConstructValue( void* ptr ) const override { new ( ptr ) ValueType{}; }
		void   destroyKey( void* ptr ) const override { static_cast<KeyType*>( ptr )->~KeyType(); }
		void   destroyValue( void* ptr ) const override { static_cast<ValueType*>( ptr )->~ValueType(); }
	};

	struct PropertyMetadata
	{
		std::string _category  = "General";
		std::string _tooltip   = "";
		float32		_minRange  = 0.0f;
		float32		_maxRange  = 1.0f;
		bool		_bHasRange = false;
	};

	struct PropertyInfo
	{

		std::shared_ptr<IContainerWrapper> _containerWrapper;

		size_t _offset = 0;

		hashed_string	 _name;
		hashed_string	 _typeName;
		hashed_string	 _elementTypeName;
		hashed_string	 _keyTypeName;
		hashed_string	 _alias;
		PropertyMetadata _metadata;

		mutable uint32 _cachedNameHash	= 0;
		mutable uint32 _cachedAliasHash = 0;

		ContainerKind _containerKind = ContainerKind::None;
		uint8		  _bIsContainer : 1;

		PropertyInfo()
			: _bIsContainer( 0 )
		{
		}

		PropertyInfo( hashed_string name, hashed_string typeName, size_t offset,
					  bool bIsContainer = false, ContainerKind containerKind = ContainerKind::None,
					  hashed_string elementTypeName = {}, hashed_string keyTypeName = {},
					  std::shared_ptr<IContainerWrapper> containerWrapper = nullptr,
					  hashed_string						 alias			  = {} )
			: _containerWrapper( std::move( containerWrapper ) )
			, _offset( offset )
			, _name( name )
			, _typeName( typeName )
			, _elementTypeName( elementTypeName )
			, _keyTypeName( keyTypeName )
			, _alias( alias )
			, _containerKind( containerKind )
			, _bIsContainer( bIsContainer ? 1 : 0 )
		{
		}

		uint32 getNameHash() const noexcept
		{
			if ( _cachedNameHash == 0 && _name.empty() == false )
				_cachedNameHash = _name.getHash();
			return _cachedNameHash;
		}

		uint32 getAliasHash() const noexcept
		{
			if ( _cachedAliasHash == 0 && _alias.empty() == false )
				_cachedAliasHash = _alias.getHash();
			return _cachedAliasHash;
		}

		bool matchesName( const hashed_string& nameOrAlias ) const
		{
			return _name == nameOrAlias || ( _alias.empty() == false && _alias == nameOrAlias );
		}

		using PropertyBindingDelegate = Delegate<void( const PropertyInfo& prop, const void* instance )>;
		mutable PropertyBindingDelegate _onPropertyBoundChanged;

		void bindOnChanged( PropertyBindingDelegate delegate ) const
		{
			_onPropertyBoundChanged = std::move( delegate );
		}

		template <typename T, typename ObjectType>
		void setValue( ObjectType* instance, const T& newValue ) const
		{
			T* ptr = reinterpret_cast<T*>( reinterpret_cast<utf8*>( instance ) + _offset );
			if ( *ptr == newValue )
				return;

			*ptr = newValue;
			if constexpr ( std::is_base_of_v<sw::IPropertyObserver, ObjectType> )
			{
				sw::IPropertyObserver* observer = static_cast<sw::IPropertyObserver*>( instance );
				observer->onPropertyChanged( _name );
			}

			if ( _onPropertyBoundChanged.isBound() )
			{
				_onPropertyBoundChanged( *this, instance );
			}
		}

		template <typename T>
		T* getValuePtr( void* instance ) const
		{
			return reinterpret_cast<T*>( reinterpret_cast<utf8*>( instance ) + _offset );
		}

		template <typename T>
		const T* getValuePtr( const void* instance ) const
		{
			return reinterpret_cast<const T*>( reinterpret_cast<const utf8*>( instance ) + _offset );
		}
	};

	struct EnumInfo
	{
		std::unordered_map<hashed_string, int64> _mapNameToValue;
		std::unordered_map<int64, hashed_string> _mapValueToName;
		hashed_string							 _name;
		hashed_string							 _fullyQualifiedName;
		hashed_string							 _moduleName;
		uint8									 _bIsBitFlag : 1;

		EnumInfo()
			: _bIsBitFlag( 0 )
		{
		}

		hashed_string toString( int64 val ) const
		{
			auto iter = _mapValueToName.find( val );
			return iter != _mapValueToName.end() ? iter->second : hashed_string();
		}

		hashed_string toStringFlags( int64 val ) const
		{
			if ( _bIsBitFlag == false )
				return toString( val );

			if ( val == 0 )
			{
				auto iter = _mapValueToName.find( 0 );
				return iter != _mapValueToName.end() ? iter->second : hashed_string( "None" );
			}

			std::string result;
			for ( const auto& [name, bitVal] : _mapNameToValue )
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

		int64 stringFlagsToValue( const std::string_view& flagsStr ) const
		{
			if ( _bIsBitFlag == false )
			{
				hashed_string nameKey( flagsStr.data(), static_cast<uint32>( flagsStr.size() ) );
				auto		  iter = _mapNameToValue.find( nameKey );
				return iter != _mapNameToValue.end() ? iter->second : 0;
			}

			int64			intResult = 0;
			/**
			 * @brief splitter 처리를 수행합니다.
			 */
			string_splitter splitter( flagsStr, { "|" } );
			for ( const auto& tokenView : splitter.getSplitList() )
			{
				std::string trimmedStr = StringUtil::trim( std::string( tokenView ) );
				if ( trimmedStr.empty() == false )
				{
					hashed_string tokenKey( trimmedStr.c_str(), static_cast<uint32>( trimmedStr.size() ) );
					auto		  iter = _mapNameToValue.find( tokenKey );
					if ( iter != _mapNameToValue.end() )
					{
						intResult |= iter->second;
					}
				}
			}

			return intResult;
		}
	};

	struct FunctionInfo
	{
		std::string									  _name;
		hashed_string								  _hashName;
		Delegate<TaskValue( void*, const TaskArgs& )> _invoker;
	};

	struct TypeInfo
	{
		hashed_string												   _name;
		hashed_string												   _fullyQualifiedName;
		hashed_string												   _parentFQN;
		hashed_string												   _moduleName;
		size_t														   _size;
		std::vector<PropertyInfo>									   _propertyList;
		std::vector<FunctionInfo>									   _methods;
		mutable std::unordered_map<hashed_string, const PropertyInfo*> _mapNameToProperty;
		mutable std::unordered_map<hashed_string, const FunctionInfo*> _mapNameToMethod;
		mutable bool												   _bIsCacheBuilt	 = false;
		mutable bool												   _bIsPODFastPath	 = false;
		mutable bool												   _bIsPODCalculated = false;

		/**
		 * @brief isPODFastPath 처리를 수행합니다.
		 */
		bool isPODFastPath() const;
		/**
		 * @brief isA 처리를 수행합니다.
		 */
		bool isA( const hashed_string& targetFqn ) const;

		void buildLookupCache() const
		{
			if ( _bIsCacheBuilt )
				return;

			for ( const sw::PropertyInfo& prop : _propertyList )
			{
				_mapNameToProperty[prop._name] = &prop;
				if ( prop._alias.empty() == false )
				{
					_mapNameToProperty[prop._alias] = &prop;
				}
			}

			for ( const sw::FunctionInfo& method : _methods )
			{
				_mapNameToMethod[method._hashName] = &method;
			}

			_bIsCacheBuilt = true;
		}

		const PropertyInfo* findProperty( const hashed_string& propNameOrAlias ) const
		{
			if ( _propertyList.size() <= 4 )
			{
				for ( const sw::PropertyInfo& prop : _propertyList )
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

		const FunctionInfo* findMethod( const hashed_string& methodName ) const
		{
			if ( _methods.size() <= 4 )
			{
				for ( const sw::FunctionInfo& method : _methods )
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

	class SW_API TypeRegistry
	{
	public:
		TypeRegistry();
		~TypeRegistry();

		TypeRegistry( const TypeRegistry& )			   = delete;
		TypeRegistry& operator=( const TypeRegistry& ) = delete;

		void registerPendingTypes( const std::string_view moduleName, struct TypeRegistrar* classHead, struct EnumRegistrar* enumHead );
		void unregisterTypesByModule( const std::string_view moduleName );

		void registerClass( const TypeInfo& info );
		void registerEnum( const EnumInfo& info );

		const TypeInfo* findType( const hashed_string& nameOrFqn ) const;
		const EnumInfo* findEnum( const hashed_string& nameOrFqn ) const;

		TaskValue invokeMethod( void* instance, const hashed_string& classFqn, const hashed_string& methodName, const TaskArgs& args = {} ) const;

	private:
		std::unordered_map<hashed_string, TypeInfo> _mapNameToClassType;
		std::unordered_map<hashed_string, EnumInfo> _mapNameToEnum;
	};

	struct SW_API TypeRegistrar
	{
		void			(*_registerFunc)(TypeRegistry&);
		TypeRegistrar*	_next;

		// DLL 경계를 넘는 단일 head (헤더 inline static 금지)
		static TypeRegistrar*& getHead();

		TypeRegistrar( void (*registerFunc)(TypeRegistry&) );
	};

	struct SW_API EnumRegistrar
	{
		void			(*_registerFunc)(TypeRegistry&);
		EnumRegistrar*	_next;

		static EnumRegistrar*& getHead();

		EnumRegistrar( void (*registerFunc)(TypeRegistry&) );
	};

	inline bool TypeInfo::isA( const hashed_string& targetFqn ) const
	{
		if ( _fullyQualifiedName == targetFqn )
			return true;
		if ( StringUtil::isNullOrEmpty( _parentFQN.c_str() ) )
			return false;

		const TypeInfo* parent = getTypeRegistry().findType( _parentFQN );
		return parent ? parent->isA( targetFqn ) : false;
	}

	template <typename T, typename = void>
	struct HasStaticType : std::false_type
	{
	};

	template <typename T>
	struct HasStaticType<T, std::void_t<decltype( T::StaticType() )>> : std::true_type
	{
	};

	template <typename T>
	inline constexpr bool HasStaticType_v = HasStaticType<T>::value;

	template <typename To, typename From>
	To* castTo( From* src )
	{
		if ( src == nullptr )
			return nullptr;

		if constexpr ( HasStaticType_v<To> )
		{
			const TypeInfo* srcType = src->getTypeInfo();
			if ( srcType != nullptr && srcType->isA( To::StaticType()._fullyQualifiedName ) )
				return static_cast<To*>( src );
			return nullptr;
		}
		else
		{
			return static_cast<To*>( src );
		}
	}
}
