/**
 * @file TelemetryEvent.h
 * @brief 텔레메트리 사건 하나 — 사건 id 와 타입이 붙은 필드 값입니다. 스키마 대조는 `TelemetryService::record` 가 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Telemetry/TelemetrySchema.h"

namespace sw
{
    /** @brief 필드 값 하나입니다. 타입에 맞는 칸만 씁니다. */
    struct TelemetryValue
    {
        string             _text{};
        hashed_string      _name{};
        float64            _number{ 0.0 };
        int64              _integer{ 0 };
        TelemetryFieldType _type{ TelemetryFieldType::String };
        uint8              _bValue{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TelemetryEvent
     * @brief 사건을 만드는 값입니다. 같은 이름을 다시 넣으면 덮어씁니다.
     * @code
     *     TelemetryEvent event( "progression.waveReached" );
     *     event.setInt( "wave", 3 ).setInt( "kills", 41 ).setFloat( "seconds", 182.5f );
     *     (void)telemetry.record( event );
     * @endcode
     */
    class SW_API TelemetryEvent
    {
    public:
        explicit TelemetryEvent( const hashed_string& eventId );

        TelemetryEvent& setBool( const hashed_string& name, bool bValue );
        TelemetryEvent& setInt( const hashed_string& name, int64 value );
        TelemetryEvent& setFloat( const hashed_string& name, float64 value );
        TelemetryEvent& setString( const hashed_string& name, string_view value );

        const hashed_string&          getId() const { return _id; }
        const vector<TelemetryValue>& getValues() const { return _listValue; }
        const TelemetryValue*         findValue( const hashed_string& name ) const;

    private:
        TelemetryValue& acquireValue( const hashed_string& name, TelemetryFieldType type );

        vector<TelemetryValue> _listValue;
        hashed_string          _id;
    };
} // namespace sw
