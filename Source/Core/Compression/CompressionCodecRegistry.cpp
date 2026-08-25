#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"

#include "Core/Compression/NullCompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"
#include "Core/Log/Logger.h"

namespace sw
{
	CompressionCodecRegistry& CompressionCodecRegistry::get()
	{
		static CompressionCodecRegistry s_instance;
		return s_instance;
	}

	CompressionCodecRegistry::CompressionCodecRegistry()
		: _mutex{}
		, _mapCodecs{}
		, _defaultCodecType{ CompressionCodecType::RLE }
	{
		registerBuiltinCodecs();
	}

	void CompressionCodecRegistry::initialize()
	{
		std::scoped_lock<mutex> lock{ _mutex };
		if ( _mapCodecs.empty() )
			registerBuiltinCodecs();
	}

	void CompressionCodecRegistry::shutdown()
	{
		std::scoped_lock<mutex> lock{ _mutex };
		_mapCodecs.clear();
	}

	void CompressionCodecRegistry::registerCodec( sw::unique_ptr<ICompressionCodec> codec )
	{
		if ( codec == nullptr )
			return;

		std::scoped_lock<mutex> lock{ _mutex };
		const uint8				key = static_cast<uint8>( codec->getCodecType() );
		SW_LOG_INFO( "[Compression] Registered codec: %# (type=%#)", codec->getCodecName(), key );
		_mapCodecs[key] = std::move( codec );
	}

	void CompressionCodecRegistry::unregisterCodec( CompressionCodecType type )
	{
		std::scoped_lock<mutex> lock{ _mutex };
		const uint8				key = static_cast<uint8>( type );
		_mapCodecs.erase( key );
	}

	ICompressionCodec* CompressionCodecRegistry::getCodec( CompressionCodecType type ) const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		const uint8				key = static_cast<uint8>( type );
		const auto				it	= _mapCodecs.find( key );
		if ( it != _mapCodecs.end() )
			return it->second.get();

		return nullptr;
	}

	ICompressionCodec* CompressionCodecRegistry::getCodec( string_view name ) const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		for ( const auto& [key, codec] : _mapCodecs )
		{
			if ( codec != nullptr && name == codec->getCodecName() )
				return codec.get();
		}
		return nullptr;
	}

	ICompressionCodec* CompressionCodecRegistry::getDefaultCodec() const
	{
		ICompressionCodec* pCodec = getCodec( _defaultCodecType );
		if ( pCodec == nullptr )
			pCodec = getCodec( CompressionCodecType::None );
		return pCodec;
	}

	CompressionCodecType CompressionCodecRegistry::getDefaultCodecType() const
	{
		return _defaultCodecType;
	}

	void CompressionCodecRegistry::setDefaultCodecType( CompressionCodecType type )
	{
		_defaultCodecType = type;
	}

	bool CompressionCodecRegistry::isCodecRegistered( CompressionCodecType type ) const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		const uint8				key = static_cast<uint8>( type );
		return _mapCodecs.find( key ) != _mapCodecs.end();
	}

	void CompressionCodecRegistry::registerBuiltinCodecs()
	{
		_mapCodecs[static_cast<uint8>( CompressionCodecType::None )] = sw::make_unique<NullCompressionCodec>();
		_mapCodecs[static_cast<uint8>( CompressionCodecType::RLE )]	 = sw::make_unique<RleCompressionCodec>();
	}
} // namespace sw
