	// --- $Name ---
	struct ${Id}_Registrar
	{
		static void RegisterType( ::sw::TypeRegistry& registry )
		{
			::sw::TypeInfo info{};
			info._name               = ::sw::hashed_string( "$Name" );
			info._fullyQualifiedName = ::sw::hashed_string( "$Name" );
			info._size               = sizeof( $CppType );
			info._bPrimitive         = 1;
			registry.registerClass( info );
$AliasRegs
		}

		${Id}_Registrar()
		{
			static ::sw::TypeRegistrar reg{ &RegisterType };
		}
	};
	static ${Id}_Registrar s_${Id}_registrar{};

