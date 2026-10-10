			registry.registerClass( info );
$AliasRegs
		}

		Registrar()
		{
			static ::sw::TypeRegistrar reg{ &RegisterType };
		}
	};
	static Registrar<$FQN> s_${ID}_registrar{};

