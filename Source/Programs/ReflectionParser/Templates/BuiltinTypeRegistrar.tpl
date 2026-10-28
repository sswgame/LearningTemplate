	// --- $Name ---
	struct ${ID}_Registrar
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

		${ID}_Registrar()
		{
			static ::sw::TypeRegistrar reg{ &RegisterType };
		}
	};
	static ${ID}_Registrar s_${ID}_registrar{};

