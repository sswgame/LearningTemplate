#pragma once
/**
 * @file TypeRegistry.h
 * @brief TypeRegistry and static Type/Enum registrar linkage
 */

#include "Core/CoreMinimal.h"
#include "Core/Reflection/ReflectionTypes.h"
#include "Core/Utility/Task/TaskTypes.h"

namespace sw
{

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

}
