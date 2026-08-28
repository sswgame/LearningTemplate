#include "pch.h"

#include "Engine/Reflection/ReflectionCore.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionConstants.h"

#include "String/hashed_string.h"

namespace sw
{
	PropertyMetadata::PropertyMetadata() noexcept
		: _category{ constants::reflection::kDefaultCategory }
		, _minRange{ 0.0f }
		, _maxRange{ 1.0f }
		, _bHasRange{ 0 }
		, _bReadOnly{ 0 }
		, _bXmlAttribute{ 0 }
		, _bAssetPath{ 0 }
		, _bPolymorphic{ 0 }
		, _reservedFlags{ 0 } {}

	FunctionMetadata::FunctionMetadata() noexcept
		: _category{ constants::reflection::kDefaultCategory }
		, _netRole{ FunctionNetRole::Local }
		, _bReliable{ 0 }
		, _bValidate{ 0 }
		, _bConstructor{ 0 }
		, _bStatic{ 0 }
		, _bConst{ 0 }
		, _reserved{ 0 } {}

	PropertyInfo::PropertyInfo() noexcept
		: _containerWrapper{ nullptr }
		, _nestedContainer{ nullptr }
		, _onPropertyBoundChanged{}
		, _offset{ 0 }
		, _name{}
		, _typeName{}
		, _elementTypeName{}
		, _keyTypeName{}
		, _listAlias{}
		, _metadata{}
		, _cachedNameHash{ 0 }
		, _containerKind{ ContainerKind::None }
		, _bIsContainer{ 0 }
		, _reservedFlags{ 0 } {}

	NestedContainerInfo PropertyInfo::getContainerShape() const
	{
		if ( _nestedContainer != nullptr )
			return *_nestedContainer;

		NestedContainerInfo flat{};
		flat._kind			  = _containerKind;
		flat._typeName		  = _typeName;
		flat._elementTypeName = _elementTypeName;
		flat._keyTypeName	  = _keyTypeName;
		flat._wrapper		  = _containerWrapper;
		return flat;
	}

	PropertyInfo::PropertyInfo( hashed_string name, hashed_string typeName, size_t offset,
								bool bIsContainer, ContainerKind containerKind,
								hashed_string elementTypeName, hashed_string keyTypeName,
								shared_ptr<IContainerWrapper> containerWrapper,
								hashed_string				  alias )
		: _containerWrapper{ std::move( containerWrapper ) }
		, _nestedContainer{ nullptr }
		, _onPropertyBoundChanged{}
		, _offset{ offset }
		, _name{ name }
		, _typeName{ typeName }
		, _elementTypeName{ elementTypeName }
		, _keyTypeName{ keyTypeName }
		, _listAlias{}
		, _metadata{}
		, _cachedNameHash{ 0 }
		, _containerKind{ containerKind }
		, _bIsContainer{ static_cast<uint8>( bIsContainer ? 1 : 0 ) }
		, _reservedFlags{ 0 }
	{
		if ( alias.empty() == false )
			_listAlias.push_back( alias );
	}

	EnumInfo::EnumInfo() noexcept
		: _invalidValue{ 0 }
		, _countValue{ 0 }
		, _bIsBitFlag{ 0 }
		, _bHasInvalid{ 0 }
		, _bHasCount{ 0 }
		, _reservedFlags{ 0 } {}

	TypeInfo::TypeInfo() noexcept
		: _size{ 0 }
		, _destroyInstance{ nullptr }
		, _name{}
		, _fullyQualifiedName{}
		, _parentFQN{}
		, _moduleName{}
		, _propertyList{}
		, _listMethod{}
		, _propertyListWithBase{}
		, _mapNameToProperty{}
		, _mapNameToMethod{}
		, _typeId{ 0 }
		, _bAbstract{ 0 }
		, _bStatic{ 0 }
		, _bPrimitive{ 0 }
		, _bIsCacheBuilt{ 0 }
		, _bIsPODFastPath{ 0 }
		, _bIsPODCalculated{ 0 }
		, _bPropertyListWithBaseBuilt{ 0 }
		, _reservedTypeFlags{ 0 }
		, _reservedPadding{ 0, 0, 0 } {}

	TypeInfo::TypeInfo( const TypeInfo& other )
		: _size{ other._size }
		, _destroyInstance{ other._destroyInstance }
		, _name{ other._name }
		, _fullyQualifiedName{ other._fullyQualifiedName }
		, _parentFQN{ other._parentFQN }
		, _moduleName{ other._moduleName }
		, _propertyList{ other._propertyList }
		, _listMethod{ other._listMethod }
		, _propertyListWithBase{}
		, _mapNameToProperty{}
		, _mapNameToMethod{}
		, _typeId{ other._typeId }
		, _bAbstract{ other._bAbstract }
		, _bStatic{ other._bStatic }
		, _bPrimitive{ other._bPrimitive }
		, _bIsCacheBuilt{ 0 }
		, _bIsPODFastPath{ 0 }
		, _bIsPODCalculated{ 0 }
		, _bPropertyListWithBaseBuilt{ 0 }
		, _reservedTypeFlags{ 0 }
		, _reservedPadding{ 0, 0, 0 }
	{
	}

	TypeInfo::TypeInfo( TypeInfo&& other ) noexcept
		: _size{ other._size }
		, _destroyInstance{ other._destroyInstance }
		, _name{ std::move( other._name ) }
		, _fullyQualifiedName{ std::move( other._fullyQualifiedName ) }
		, _parentFQN{ std::move( other._parentFQN ) }
		, _moduleName{ std::move( other._moduleName ) }
		, _propertyList{ std::move( other._propertyList ) }
		, _listMethod{ std::move( other._listMethod ) }
		, _propertyListWithBase{}
		, _mapNameToProperty{}
		, _mapNameToMethod{}
		, _typeId{ other._typeId }
		, _bAbstract{ other._bAbstract }
		, _bStatic{ other._bStatic }
		, _bPrimitive{ other._bPrimitive }
		, _bIsCacheBuilt{ 0 }
		, _bIsPODFastPath{ 0 }
		, _bIsPODCalculated{ 0 }
		, _bPropertyListWithBaseBuilt{ 0 }
		, _reservedTypeFlags{ 0 }
		, _reservedPadding{ 0, 0, 0 }
	{
		other._typeId		   = 0;
		other._size			   = 0;
		other._destroyInstance = nullptr;
		other._bIsCacheBuilt   = 0;
	}

	TypeInfo& TypeInfo::operator=( const TypeInfo& other )
	{
		if ( this == &other )
			return *this;

		_size				= other._size;
		_destroyInstance	= other._destroyInstance;
		_name				= other._name;
		_fullyQualifiedName = other._fullyQualifiedName;
		_parentFQN			= other._parentFQN;
		_moduleName			= other._moduleName;
		_propertyList		= other._propertyList;
		_listMethod			= other._listMethod;
		_typeId				= other._typeId;
		_bAbstract			= other._bAbstract;
		_bStatic			= other._bStatic;
		_bPrimitive			= other._bPrimitive;

		_propertyListWithBase.clear();
		_mapNameToProperty.clear();
		_mapNameToMethod.clear();
		_bIsCacheBuilt				= 0;
		_bIsPODFastPath				= 0;
		_bIsPODCalculated			= 0;
		_bPropertyListWithBaseBuilt = 0;

		return *this;
	}

	TypeInfo& TypeInfo::operator=( TypeInfo&& other ) noexcept
	{
		if ( this == &other )
			return *this;

		_size				= other._size;
		_destroyInstance	= other._destroyInstance;
		_name				= std::move( other._name );
		_fullyQualifiedName = std::move( other._fullyQualifiedName );
		_parentFQN			= std::move( other._parentFQN );
		_moduleName			= std::move( other._moduleName );
		_propertyList		= std::move( other._propertyList );
		_listMethod			= std::move( other._listMethod );
		_typeId				= other._typeId;
		_bAbstract			= other._bAbstract;
		_bStatic			= other._bStatic;
		_bPrimitive			= other._bPrimitive;

		_propertyListWithBase.clear();
		_mapNameToProperty.clear();
		_mapNameToMethod.clear();
		_bIsCacheBuilt				= 0;
		_bIsPODFastPath				= 0;
		_bIsPODCalculated			= 0;
		_bPropertyListWithBaseBuilt = 0;

		other._typeId		   = 0;
		other._size			   = 0;
		other._destroyInstance = nullptr;
		other._bIsCacheBuilt   = 0;

		return *this;
	}

	bool TypeInfo::usesPodCopyFastPath() const
	{
		if ( _bIsPODCalculated )
			return _bIsPODFastPath;

		TypeRegistry& registry = engine::getTypeRegistry();

		static const hashed_string kArrDynamicTypes[] = {
			hashed_string{ PredefinedNameType::NameType_string },
			hashed_string{ PredefinedNameType::NameType_hashed_string },
		};

		_bIsPODFastPath = SW_TRUE;
		for ( const PropertyInfo& prop : _propertyList )
		{
			if ( prop._bIsContainer != 0 || prop._containerKind != ContainerKind::None )
			{
				_bIsPODFastPath = SW_FALSE;
				break;
			}

			bool bIsDynamicType = false;
			for ( const hashed_string& dynamicType : kArrDynamicTypes )
			{
				if ( registry.isType( prop._typeName, dynamicType ) )
				{
					bIsDynamicType = true;
					break;
				}
			}

			if ( bIsDynamicType )
			{
				_bIsPODFastPath = SW_FALSE;
				break;
			}
		}
		_bIsPODCalculated = SW_TRUE;
		return _bIsPODFastPath;
	}

	const vector<PropertyInfo>& TypeInfo::getPropertiesWithBase() const
	{
		if ( _parentFQN.empty() )
			return _propertyList;

		if ( _bPropertyListWithBaseBuilt != 0 )
			return _propertyListWithBase;

		const TypeInfo*				pParent		 = engine::getTypeRegistry().findType( _parentFQN );
		const vector<PropertyInfo>* pParentProps = ( pParent != nullptr ) ? &pParent->getPropertiesWithBase() : nullptr;
		const size_t				totalCount	 = ( pParentProps != nullptr ? pParentProps->size() : 0 ) + _propertyList.size();

		_propertyListWithBase.clear();
		_propertyListWithBase.reserve( totalCount );
		if ( pParentProps != nullptr )
		{
			_propertyListWithBase = *pParentProps;
		}

		for ( const PropertyInfo& prop : _propertyList )
		{
			bool replaced{ false };
			for ( PropertyInfo& existing : _propertyListWithBase )
			{
				if ( existing._name == prop._name )
				{
					existing = prop;
					replaced = true;
					break;
				}
			}
			if ( replaced == false )
				_propertyListWithBase.push_back( prop );
		}

		_bPropertyListWithBaseBuilt = SW_TRUE;
		return _propertyListWithBase;
	}

	namespace generated
	{
		void forceLinkBuiltinTypes();
	} // namespace generated

	TypeRegistry::TypeRegistry()
	{
		generated::forceLinkBuiltinTypes();
	}
	TypeRegistry::~TypeRegistry() = default;

	static std::atomic<uint32> _s_typeIdCounter{ 0 }; // Local Runtime Index (Not Serialized, 100% Cross-Platform Safe)

	void TypeRegistry::registerClass( const TypeInfo& info )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };

		TypeInfo stored = info;
		if ( _activeModuleName.empty() == false )
			stored._moduleName = _activeModuleName;
		else if ( stored._moduleName.empty() )
			stored._moduleName = hashed_string( constants::reflection::kDefaultModuleName );

		auto existingIt = _mapNameToClassType.find( stored._fullyQualifiedName );
		if ( existingIt != _mapNameToClassType.end() )
		{
			stored._typeId = existingIt->second._typeId;
		}
		else
		{
			stored._typeId = _s_typeIdCounter.fetch_add( 1, std::memory_order_relaxed ) + 1;
		}

		const hashed_string canonicalName = stored._name.empty() == false ? stored._name : stored._fullyQualifiedName;

		_mapNameToClassType.insert_or_assign( stored._fullyQualifiedName, stored );
		_mapHashToCanonicalName.insert_or_assign( stored._fullyQualifiedName.getHash(), canonicalName );
		if ( stored._name.empty() == false && stored._name != stored._fullyQualifiedName )
		{
			_mapNameToClassType.insert_or_assign( stored._name, stored );
			_mapHashToCanonicalName.insert_or_assign( stored._name.getHash(), canonicalName );
		}
	}

	void TypeRegistry::registerEnum( const EnumInfo& info )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		EnumInfo							stored = info;
		if ( _activeModuleName.empty() == false )
			stored._moduleName = _activeModuleName;
		else if ( stored._moduleName.empty() )
			stored._moduleName = hashed_string( constants::reflection::kDefaultModuleName );

		_mapNameToEnum.insert_or_assign( stored._fullyQualifiedName, stored );
		if ( stored._name.empty() == false && stored._name != stored._fullyQualifiedName )
			_mapNameToEnum.insert_or_assign( stored._name, stored );
	}

	namespace
	{
		/**
		 * @brief canonical FQN 의 네임스페이스를 alias 앞에 붙입니다.
		 * @details REFLECT(Alias=Foo) 는 리프 이름만 적으므로, registerClass 가 FQN·리프를
		 *          모두 등록하는 것과 맞추려면 별칭도 FQN 형태를 함께 등록해야 합니다.
		 * @return 네임스페이스가 없거나 alias 가 이미 한정되어 있으면 빈 문자열.
		 */
		string qualifyAliasWithNamespace( const utf8* pAliasName, const utf8* pCanonicalName )
		{
			const string_view alias{ pAliasName };
			const string_view canonical{ pCanonicalName };
			if ( alias.find( constants::reflection::kScopeDelimiter ) != string_view::npos )
				return {};

			const size_t lastScope = canonical.rfind( constants::reflection::kScopeDelimiter );
			if ( lastScope == string_view::npos )
				return {};

			string qualified{ canonical.substr( 0, lastScope + 2 ) };
			qualified.append( alias.data(), alias.size() );
			return qualified;
		}
	} // namespace

	void TypeRegistry::registerPendingTypes( string_view moduleName, TypeRegistrar* pClassHead, EnumRegistrar* pEnumHead )
	{
		_activeModuleName = hashed_string( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

		TypeRegistrar* pCurrClass = pClassHead;
		while ( pCurrClass != nullptr )
		{
			if ( pCurrClass->_registerFunc != nullptr )
				pCurrClass->_registerFunc( *this );
			pCurrClass = pCurrClass->_pNext;
		}

		EnumRegistrar* pCurrEnum = pEnumHead;
		while ( pCurrEnum != nullptr )
		{
			if ( pCurrEnum->_registerFunc != nullptr )
				pCurrEnum->_registerFunc( *this );
			pCurrEnum = pCurrEnum->_pNext;
		}

		_activeModuleName = hashed_string();
	}

	void TypeRegistry::unregisterTypesByModule( string_view moduleName )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		hashed_string						hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

		for ( auto it = _mapNameToClassType.begin(); it != _mapNameToClassType.end(); )
		{
			if ( it->second._moduleName == hashModule )
				it = _mapNameToClassType.erase( it );
			else
				++it;
		}

		for ( auto it = _mapNameToEnum.begin(); it != _mapNameToEnum.end(); )
		{
			if ( it->second._moduleName == hashModule )
				it = _mapNameToEnum.erase( it );
			else
				++it;
		}

		_mapHashToCanonicalName.clear();
		for ( const auto& [key, info] : _mapNameToClassType )
		{
			const hashed_string canonicalName = info._name.empty() == false ? info._name : info._fullyQualifiedName;
			_mapHashToCanonicalName.insert_or_assign( key.getHash(), canonicalName );
		}
	}

	void TypeRegistry::registerTypeAlias( const utf8* pAliasName, const utf8* pCanonicalName )
	{
		if ( pAliasName == nullptr || pCanonicalName == nullptr )
			return;
		if ( StringUtil::strcmp( pAliasName, pCanonicalName ) == 0 )
			return;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		auto								it = _mapNameToClassType.find( hashed_string( pCanonicalName ) );
		if ( it == _mapNameToClassType.end() )
			return;

		// insert_or_assign: 핫리로드 재등록 시 옛 TypeInfo 복사본이 남지 않게 함.
		const TypeInfo		stored		  = it->second;
		const hashed_string canonicalName = stored._name.empty() == false ? stored._name : stored._fullyQualifiedName;
		const hashed_string aliasHash{ pAliasName };

		_mapNameToClassType.insert_or_assign( aliasHash, stored );
		_mapHashToCanonicalName.insert_or_assign( aliasHash.getHash(), canonicalName );

		const string qualified = qualifyAliasWithNamespace( pAliasName, pCanonicalName );
		if ( qualified.empty() == false )
		{
			const hashed_string qualHash{ qualified.c_str() };
			_mapNameToClassType.insert_or_assign( qualHash, stored );
			_mapHashToCanonicalName.insert_or_assign( qualHash.getHash(), canonicalName );
		}
	}

	void TypeRegistry::registerEnumAlias( const utf8* pAliasName, const utf8* pCanonicalName )
	{
		if ( pAliasName == nullptr || pCanonicalName == nullptr )
			return;
		if ( StringUtil::strcmp( pAliasName, pCanonicalName ) == 0 )
			return;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		auto								it = _mapNameToEnum.find( hashed_string( pCanonicalName ) );
		if ( it == _mapNameToEnum.end() )
			return;

		const EnumInfo stored = it->second;
		_mapNameToEnum.insert_or_assign( hashed_string( pAliasName ), stored );

		const string qualified = qualifyAliasWithNamespace( pAliasName, pCanonicalName );
		if ( qualified.empty() == false )
			_mapNameToEnum.insert_or_assign( hashed_string( qualified.c_str() ), stored );
	}

	const TypeInfo* TypeRegistry::findType( const hashed_string& nameOrFqn ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		auto								it = _mapNameToClassType.find( nameOrFqn );
		return it != _mapNameToClassType.end() ? &it->second : nullptr;
	}

	const EnumInfo* TypeRegistry::findEnum( const hashed_string& nameOrFqn ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		auto								it = _mapNameToEnum.find( nameOrFqn );
		return it != _mapNameToEnum.end() ? &it->second : nullptr;
	}

	hashed_string TypeRegistry::canonicalTypeNameByHash( const uint32 nameHash ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		auto								it = _mapHashToCanonicalName.find( nameHash );
		return it != _mapHashToCanonicalName.end() ? it->second : hashed_string{};
	}

	const utf8* TypeRegistry::enumToString( const hashed_string& enumName, int64 value ) const
	{
		const EnumInfo* pInfo = findEnum( enumName );
		return pInfo != nullptr ? pInfo->valueToCString( value ) : nullptr;
	}

	bool TypeRegistry::enumFromString( const hashed_string& enumName, string_view name, int64& outValue ) const
	{
		const EnumInfo* pInfo = findEnum( enumName );
		if ( pInfo == nullptr )
		{
			outValue = 0;
			return false;
		}
		if ( pInfo->tryParse( name, outValue ) && pInfo->isValidValue( outValue ) )
			return true;
		outValue = ( pInfo->_bHasInvalid != 0 ) ? pInfo->_invalidValue : 0;
		return false;
	}

	bool TypeRegistry::hasFlag( const hashed_string& enumName, int64 flags, int64 contains ) const
	{
		const EnumInfo* pInfo = findEnum( enumName );
		if ( pInfo == nullptr || pInfo->_bIsBitFlag == 0 )
			return false;
		return ( flags & contains ) == contains;
	}

	TaskValue TypeRegistry::invokeMethod( void* pInstance, const hashed_string& classFqn, const hashed_string& methodName, const TaskArgs& args ) const
	{
		const TypeInfo* pTypeInfo = findType( classFqn );
		if ( pTypeInfo != nullptr )
		{
			const FunctionInfo* pFunc = pTypeInfo->findMethod( methodName );
			if ( pFunc != nullptr && pFunc->_invoker.isBound() )
				return pFunc->_invoker( pInstance, args );
		}
		return TaskValue{};
	}

	TypeRegistrar*& TypeRegistrar::getHead()
	{
		static TypeRegistrar* s_pHead{ nullptr };
		return s_pHead;
	}

	TypeRegistrar::TypeRegistrar( void ( *registerFunc )( TypeRegistry& ) )
		: TypeRegistrar( registerFunc, getHead() )
	{
	}

	TypeRegistrar::TypeRegistrar( void ( *registerFunc )( TypeRegistry& ), TypeRegistrar*& pModuleHead )
		: _registerFunc{ registerFunc }
		, _pNext{ nullptr }
	{
		_pNext		= pModuleHead;
		pModuleHead = this;
	}

	EnumRegistrar*& EnumRegistrar::getHead()
	{
		static EnumRegistrar* s_pHead{ nullptr };
		return s_pHead;
	}

	EnumRegistrar::EnumRegistrar( void ( *registerFunc )( TypeRegistry& ) )
		: EnumRegistrar( registerFunc, getHead() )
	{
	}

	EnumRegistrar::EnumRegistrar( void ( *registerFunc )( TypeRegistry& ), EnumRegistrar*& pModuleHead )
		: _registerFunc{ registerFunc }
		, _pNext{ nullptr }
	{
		_pNext		= pModuleHead;
		pModuleHead = this;
	}
} // namespace sw
namespace sw
{

	bool TypeInfo::isDerivedFrom( const hashed_string& targetFqn ) const
	{
		if ( _fullyQualifiedName == targetFqn || _name == targetFqn )
			return true;
		if ( _parentFQN.empty() )
			return false;

		const TypeRegistry& registry = engine::getTypeRegistry();
		const TypeInfo*		pCurrent = this;
		while ( pCurrent != nullptr && pCurrent->_parentFQN.empty() == false )
		{
			if ( pCurrent->_parentFQN == targetFqn )
				return true;
			pCurrent = registry.findType( pCurrent->_parentFQN );
			if ( pCurrent != nullptr && ( pCurrent->_fullyQualifiedName == targetFqn || pCurrent->_name == targetFqn ) )
				return true;
		}
		return false;
	}

	const PropertyInfo* TypeInfo::findPropertyInHierarchy( const hashed_string& propNameOrAlias ) const
	{
		const PropertyInfo* pProp = findProperty( propNameOrAlias );
		if ( pProp != nullptr )
			return pProp;

		if ( _parentFQN.empty() )
			return nullptr;

		const TypeInfo* pParent = engine::getTypeRegistry().findType( _parentFQN );
		return pParent != nullptr ? pParent->findPropertyInHierarchy( propNameOrAlias ) : nullptr;
	}
} // namespace sw
