			registry.registerEnum( info );
$AliasRegs
		}

		Registrar()
		{
			static ::sw::EnumRegistrar reg{ &RegisterEnum };
		}
	};
	static Registrar<$FQN> s_${Id}_registrar{};

