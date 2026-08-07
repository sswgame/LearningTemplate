/**
 * @file ReflectionCore.cpp
 * @brief Reflection type registry and registrar implementations (DLL-safe, non-inline STL ops)
 */
#include "pch.h"
#include "Core/Reflection/ReflectionCore.h"

namespace sw
{
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
		_mapNameToClassType.insert_or_assign( info._fullyQualifiedName, info );
		if ( info._name.empty() == false )
		{
			_mapNameToClassType.try_emplace( info._name, info );
		}
	}

	void TypeRegistry::registerEnum( const EnumInfo& info )
	{
		_mapNameToEnum.insert_or_assign( info._fullyQualifiedName, info );
		if ( info._name.empty() == false )
		{
			_mapNameToEnum.try_emplace( info._name, info );
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
		static TypeRegistrar* head = nullptr;
		return head;
	}

	TypeRegistrar::TypeRegistrar( void (*registerFunc)(TypeRegistry&) )
		: _registerFunc( registerFunc )
		, _next( nullptr )
	{
		_next	  = getHead();
		getHead() = this;
	}

	EnumRegistrar*& EnumRegistrar::getHead()
	{
		static EnumRegistrar* head = nullptr;
		return head;
	}

	EnumRegistrar::EnumRegistrar( void (*registerFunc)(TypeRegistry&) )
		: _registerFunc( registerFunc )
		, _next( nullptr )
	{
		_next	  = getHead();
		getHead() = this;
	}

	void TypeRegistry::registerPendingTypes( const std::string_view moduleName, TypeRegistrar* classHead, EnumRegistrar* enumHead )
	{
		(void)moduleName;

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
	}

	void registerCoreReflectionTypes()
	{
		getTypeRegistry().registerPendingTypes( "Core", TypeRegistrar::getHead(), EnumRegistrar::getHead() );
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
}
