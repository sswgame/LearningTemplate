			registry.registerClass( info );
$AliasRegs
		}

		Registrar()
		{
			static ::sw::TypeRegistrar reg{ &RegisterType };
		}
	};
	static Registrar<$FQN> s_${Id}_registrar{};

