#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <optional>
#include <charconv>
#include <type_traits>
#include <cstdint>
#include <functional>
#include <cassert>

namespace gamescope
{
    class ConCommand;

    template <typename T>
    inline __attribute__((cold)) std::optional<T> Parse( std::string_view chars )
    {
        T obj;
        auto result = std::from_chars( chars.begin(), chars.end(), obj );
        if ( result.ec == std::errc{} )
            return obj;
        else
            return std::nullopt;
    }

    template <>
    inline __attribute__((cold)) std::optional<bool> Parse( std::string_view chars )
    {
        std::optional<uint32_t> oNumber = Parse<uint32_t>( chars );
        if ( oNumber )
            return !!*oNumber;

        if ( chars == "true" )
            return true;
        else
            return false;
    }

    struct StringHash
    {
        using is_transparent = void;
        [[nodiscard]] size_t operator()( const char *string )        const { return std::hash<std::string_view>{}( string ); }
        [[nodiscard]] size_t operator()( std::string_view string )   const { return std::hash<std::string_view>{}( string ); }
        [[nodiscard]] size_t operator()( const std::string &string ) const { return std::hash<std::string>{}( string ); }
    };

    template <typename T>
    using Dict = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

    class ConCommand
    {
        using ConCommandFunc = std::function<void( std::span<std::string_view> )>;

    public:
        __attribute__((cold)) ConCommand( std::string_view pszName, std::string_view pszDescription, ConCommandFunc func )
            : m_pszName{ pszName }
            , m_pszDescription{ pszDescription }
            , m_Func{ func }
        {
            assert( !GetCommands().contains( pszName ) );
            GetCommands()[ std::string( pszName ) ] = this;
        }

        __attribute__((cold)) ~ConCommand()
        {
            GetCommands().erase( GetCommands().find( m_pszName ) );
        }

        void __attribute__((cold)) Invoke( std::span<std::string_view> args )
        {
            if ( m_Func )
                m_Func( args );
        }

        static Dict<ConCommand *>& __attribute__((cold)) GetCommands();
    protected:
        std::string_view m_pszName;
        std::string_view m_pszDescription;
        ConCommandFunc m_Func;
    };

    template <typename T>
    class ConVar : public ConCommand
    {
        using ConVarCallbackFunc = std::function<void()>;
    public:
        __attribute__((cold)) ConVar( std::string_view pszName, T defaultValue = T{}, std::string_view pszDescription = "", ConVarCallbackFunc func = nullptr )
            : ConCommand( pszName, pszDescription, [this]( std::span<std::string_view> pArgs ){ this->InvokeFunc( pArgs ); } )
            , m_Value{ defaultValue }
            , m_Callback{ func }
        {
        }

        const T& __attribute__((cold)) Get() const
        {
            return m_Value;
        }

        template <typename J>
        void __attribute__((cold)) SetValue( const J &newValue )
        {
            m_Value = T{ newValue };

            if ( !m_bInCallback && m_Callback )
            {
                m_bInCallback = true;
                m_Callback();
                m_bInCallback = false;
            }
        }

        template <typename J>
        ConVar<T>& __attribute__((cold)) operator =( const J &newValue ) { SetValue<J>( newValue ); return *this; }

        operator __attribute__((cold)) T() const { return m_Value; }

        template <typename J> bool __attribute__((cold)) operator == ( const J &other ) const { return m_Value ==  other; }
        template <typename J> bool __attribute__((cold)) operator != ( const J &other ) const { return m_Value !=  other; }
        template <typename J> bool __attribute__((cold)) operator <=>( const J &other ) const { return m_Value <=> other; }

        void __attribute__((cold)) InvokeFunc( std::span<std::string_view> pArgs )
        {
            if ( pArgs.size() != 2 )
                return;

            if constexpr ( std::is_integral<T>::value )
            {
                std::optional<T> oResult = Parse<T>( pArgs[1] );
                SetValue( oResult ? *oResult : T{} );
            }
            else
            {
                SetValue( pArgs[1] );
            }
        }
    private:
        T m_Value{};
        ConVarCallbackFunc m_Callback;
        bool m_bInCallback;
    };
}
