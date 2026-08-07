/**
 * @file ReflectionCore.cpp
 * @brief Reflection type registry and registrar implementations (DLL-safe, non-inline STL ops)
 */
#include "pch.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Common/CoreServices.h"

namespace sw
{
	PropertyMetadata::PropertyMetadata() noexcept
		: _bHasRange{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	PropertyInfo::PropertyInfo() noexcept
		: _bIsContainer{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	PropertyInfo::PropertyInfo( hashed_string name, hashed_string typeName, size_t offset,
								bool bIsContainer, ContainerKind containerKind,
								hashed_string elementTypeName, hashed_string keyTypeName,
								std::shared_ptr<IContainerWrapper> containerWrapper,
								hashed_string					   alias )
		: _containerWrapper{ std::move( containerWrapper ) }
		, _offset{ offset }
		, _name{ name }
		, _typeName{ typeName }
		, _elementTypeName{ elementTypeName }
		, _keyTypeName{ keyTypeName }
		, _alias{ alias }
		, _containerKind{ containerKind }
		, _bIsContainer{ static_cast<uint8>( bIsContainer ? 1 : 0 ) }
		, _reservedFlags{ 0 }
	{
	}

	EnumInfo::EnumInfo() noexcept
		: _bIsBitFlag{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	bool TypeInfo::isPODFastPath() const
	{
		if ( _bIsPODCalculated )
			return _bIsPODFastPath;

		static const hashed_string kStdString( "std::string" );
		static const hashed_string kString( "string" );

		_bIsPODFastPath = true;
		for ( const PropertyInfo& prop : _propertyList )
		{
			if ( prop._bIsContainer != 0 || prop._containerKind != ContainerKind::None )
			{
				_bIsPODFastPath = false;
				break;
			}

			if ( prop._typeName == kStdString || prop._typeName == kString )
			{
				_bIsPODFastPath = false;
				break;
			}
		}
		_bIsPODCalculated = true;
		return _bIsPODFastPath;
	}

	TypeRegistry::TypeRegistry()  = default;
	TypeRegistry::~TypeRegistry() = default;

	void TypeRegistry::registerClass( const TypeInfo& info )
	{
		TypeInfo stored = info;
		if ( stored._moduleName.empty() && _activeModuleName.empty() == false )
			stored._moduleName = _activeModuleName;

		_mapNameToClassType.insert_or_assign( stored._fullyQualifiedName, stored );
		if ( stored._name.empty() == false )
		{
			_mapNameToClassType.try_emplace( stored._name, stored );
		}
	}

	void TypeRegistry::registerEnum( const EnumInfo& info )
	{
		EnumInfo stored = info;
		if ( stored._moduleName.empty() && _activeModuleName.empty() == false )
			stored._moduleName = _activeModuleName;

		_mapNameToEnum.insert_or_assign( stored._fullyQualifiedName, stored );
		if ( stored._name.empty() == false )
		{
			_mapNameToEnum.try_emplace( stored._name, stored );
		}
	}

	const TypeInfo* TypeRegistry::findType( const hashed_string& nameOrFqn ) const
	{
		auto it = _mapNameToClassType.find( nameOrFqn );
		return it != _mapNameToClassType.end() ? &it->second : nullptr;
	}

	const EnumInfo* TypeRegistry::findEnum( const hashed_string& nameOrFqn ) const
	{
		auto it = _mapNameToEnum.find( nameOrFqn );
		return it != _mapNameToEnum.end() ? &it->second : nullptr;
	}

	TaskValue TypeRegistry::invokeMethod( void* instance, const hashed_string& classFqn, const hashed_string& methodName, const TaskArgs& args ) const
	{
		const TypeInfo* typeInfo = findType( classFqn );
		if ( typeInfo != nullptr )
		{
			const FunctionInfo* func = typeInfo->findMethod( methodName );
			if ( func != nullptr && func->_invoker.isBound() )
			{
				return func->_invoker( instance, args );
			}
		}
		return TaskValue{};
	}

	TypeRegistrar*& TypeRegistrar::getHead()
	{
		static TypeRegistrar* s_head = nullptr;
		return s_head;
	}

	TypeRegistrar::TypeRegistrar( void ( *registerFunc )( TypeRegistry& ) )
		: TypeRegistrar( registerFunc, getHead() )
	{
	}

	TypeRegistrar::TypeRegistrar( void ( *registerFunc )( TypeRegistry& ), TypeRegistrar*& moduleHead )
		: _registerFunc{ registerFunc }
		, _next{ nullptr }
	{
		_next	   = moduleHead;
		moduleHead = this;
	}

	EnumRegistrar*& EnumRegistrar::getHead()
	{
		static EnumRegistrar* s_head = nullptr;
		return s_head;
	}

	EnumRegistrar::EnumRegistrar( void ( *registerFunc )( TypeRegistry& ) )
		: EnumRegistrar( registerFunc, getHead() )
	{
	}

	EnumRegistrar::EnumRegistrar( void ( *registerFunc )( TypeRegistry& ), EnumRegistrar*& moduleHead )
		: _registerFunc{ registerFunc }
		, _next{ nullptr }
	{
		_next	   = moduleHead;
		moduleHead = this;
	}

	void TypeRegistry::registerPendingTypes( const std::string_view moduleName, TypeRegistrar* classHead, EnumRegistrar* enumHead )
	{
		_activeModuleName = hashed_string( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

		TypeRegistrar* currClass = classHead;
		while ( currClass != nullptr )
		{
			if ( currClass->_registerFunc != nullptr )
			{
				currClass->_registerFunc( *this );
			}
			currClass = currClass->_next;
		}

		EnumRegistrar* currEnum = enumHead;
		while ( currEnum != nullptr )
		{
			if ( currEnum->_registerFunc != nullptr )
			{
				currEnum->_registerFunc( *this );
			}
			currEnum = currEnum->_next;
		}

		_activeModuleName = hashed_string();
	}

	void registerCoreReflectionTypes()
	{
		getTypeRegistry().registerPendingTypes( "Core", TypeRegistrar::getHead(), EnumRegistrar::getHead() );
		// ComponentManager is bound before this call (App::initializeSubsystems).
		getComponentManager().registerPendingFactories( ComponentFactoryRegistrar::getHead() );
	}

	void TypeRegistry::unregisterTypesByModule( const std::string_view moduleName )
	{
		hashed_string hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

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
	}
} // namespace sw
