/*++
 */
#ifndef WINDOWS_TERMAINL_ARGV_HPP
#define WINDOWS_TERMAINL_ARGV_HPP
#pragma once
#include <string>
#include <string_view>
#include <functional>
#include <vector>
#include "strcat.hpp"

namespace av
{
    enum ParseError
    {
        SkipParse = -1,
        None = 0,
        ErrorNormal = 1 //
    };
    // not like base::error_code
    struct error_code
    {
        std::wstring message;
        int ec{ 0 };
        explicit operator bool() const noexcept
        {
            return ec != None;
        }
        void Assign(std::wstring_view msg, int val = ErrorNormal)
        {
            ec = val;
            message.assign(msg);
        }
        template <typename... Args>
        void Assign(int val, Args... args)
        {
            ec = val;
            message = base::strings_internal::CatPieces({ static_cast<const base::AlphaNum&>(args).Piece()... });
        }
    };
    enum HasArgs
    {
        required_argument, /// -i 11 or -i=xx
        no_argument,
        optional_argument /// -s --long --long=xx
    };
    constexpr const int NoneVal = 0;
    struct option
    {
        std::wstring_view name;
        HasArgs has_args;
        int val;
    };

    using invoke_t = std::function<bool(int, const wchar_t*, const wchar_t*)>;
    // Parser to resolve long or short argument
    class Parser
    {
    public:
        using StringArray = std::vector<std::wstring_view>;
        Parser(int argc, wchar_t* const* argv, bool se = false)
            : argc_(argc)
            , argv_(argv)
            , subcmdenabled(se)
        {
        }
        Parser(const Parser&) = delete;
        Parser& operator=(const Parser&) = delete;
        Parser& Add(std::wstring_view name, HasArgs a, int val)
        {
            options_.push_back({ name, a, val });
            return *this;
        }
        bool Execute(const invoke_t& v, error_code& ec);
        const StringArray& UnresolvedArgs() const
        {
            return uargs;
        }

    private:
        int argc_;
        wchar_t* const* argv_;
        int index{ 0 };
        bool subcmdenabled{ false };
        StringArray uargs;
        std::vector<option> options_;
        bool parse_internal(std::wstring_view a, const invoke_t& v, error_code& ec);
        bool parse_internal_long(std::wstring_view a, const invoke_t& v, error_code& ec);
        bool parse_internal_short(std::wstring_view a, const invoke_t& v, error_code& ec);
    };

    // ---------> parse internal
    inline bool Parser::Execute(const invoke_t& v, error_code& ec)
    {
        if (argc_ == 0 || argv_ == nullptr)
        {
            ec.Assign(L"the command line array is empty.");
            return false;
        }
        index = 1;
        for (; index < argc_; index++)
        {
            std::wstring_view a = argv_[index];
            if (a.empty() || a.front() != '-')
            {
                // When we turn on subcommand mode, we stop parsing
                if (subcmdenabled)
                {
                    for (int i = index; i < argc_; i++)
                    {
                        uargs.push_back(argv_[i]);
                    }
                    return true;
                }
                uargs.push_back(a);
                continue;
            }
            // parse ---
            if (!parse_internal(a, v, ec))
            {
                return false;
            }
        }
        return true;
    }

    inline bool Parser::parse_internal_short(std::wstring_view a, const invoke_t& v, error_code& ec)
    {
        int ch = -1;
        HasArgs ha = optional_argument;
        const wchar_t* oa = nullptr;
        // -x=XXX
        // -xXXX
        // -x XXX
        // -x; BOOL
        if (a[0] == '=')
        {
            ec.Assign(1, L"unexpected argument '-", a, L"'"); // -=*
            return false;
        }
        auto c = a[0];
        for (const auto& o : options_)
        {
            if (o.val == c)
            {
                ch = o.val;
                ha = o.has_args;
                break;
            }
        }
        if (ch == -1)
        {
            ec.Assign(1, L"unregistered option '-", a, L"'");
            return false;
        }
        // a.size()==1 'L' short value
        if (a.size() >= 2)
        {
            oa = (a[1] == L'=') ? (a.data() + 2) : (a.data() + 1);
        }
        if (oa != nullptr && ha == no_argument)
        {
            ec.Assign(1, L"option '-", a.substr(0, 1), L"' unexpected parameter: ", oa);
            return false;
        }
        if (oa == nullptr && ha == required_argument)
        {
            if (index + 1 >= argc_)
            {
                ec.Assign(1, L"option '-", a, L"' missing parameter");
                return false;
            }
            oa = argv_[index + 1];
            index++;
        }
        if (!v(ch, oa, a.data()))
        {
            ec.Assign(SkipParse, L"skip parse");
            return false;
        }
        return true;
    }

    // Parse long option
    inline bool Parser::parse_internal_long(std::wstring_view a, const invoke_t& v, error_code& ec)
    {
        // --xxx=XXX
        // --xxx XXX
        // --xxx; bool
        int ch = -1;
        HasArgs ha = optional_argument;
        const wchar_t* oa = nullptr;
        auto pos = a.find(L'=');
        if (pos != std::string_view::npos)
        {
            if (pos + 1 >= a.size())
            {
                ec.Assign(1, L"unexpected argument '--", a, L"'");
                return false;
            }
            oa = a.data() + pos + 1;
            a.remove_suffix(a.size() - pos);
        }
        for (const auto& o : options_)
        {
            if (o.name == a)
            {
                ch = o.val;
                ha = o.has_args;
                break;
            }
        }
        if (ch == -1)
        {
            ec.Assign(1, L"unregistered option '--", a, L"'");
            return false;
        }
        if (oa != nullptr && ha == no_argument)
        {
            ec.Assign(1, L"option '--", a, L"' unexpected parameter: ", oa);
            return false;
        }
        if (oa == nullptr && ha == required_argument)
        {
            if (index + 1 >= argc_)
            {
                ec.Assign(1, L"option '--", a, L"' missing parameter");
                return false;
            }
            oa = argv_[index + 1];
            index++;
        }
        if (!v(ch, oa, a.data()))
        {
            ec.Assign(SkipParse, L"skip parse");
            return false;
        }
        return true;
    }

    inline bool Parser::parse_internal(std::wstring_view a, const invoke_t& v, error_code& ec)
    {
        if (a.size() == 1)
        {
            ec.Assign(L"unexpected argument '-'");
            return false;
        }
        if (a[1] == '-')
        {
            return parse_internal_long(a.substr(2), v, ec);
        }
        return parse_internal_short(a.substr(1), v, ec);
    }
    // Rebuild argument
    class Builder
    {
    public:
        Builder() = default;
        Builder(const Builder&) = delete;
        Builder& operator=(const Builder&) = delete;
        Builder& Assign(std::wstring_view a0)
        {
            args_.assign(Escape(a0));
            return *this;
        }
        Builder& AssignNoEscape(std::wstring_view a0)
        {
            args_.assign(a0);
            return *this;
        }
        Builder& Append(std::wstring_view a)
        {
            args_.append(L" ").append(Escape(a));
            return *this;
        }
        const std::wstring& Args() const
        {
            return args_;
        }
        // C++17 model
        wchar_t* data()
        {
            return args_.data();
        }

    private:
        std::wstring args_;
        std::wstring Escape(std::wstring_view ac);
    };

    inline std::wstring Builder::Escape(std::wstring_view ac)
    {
        if (ac.empty())
        {
            return L"\"\"";
        }
        bool hasspace = false;
        auto n = ac.size();
        for (auto c : ac)
        {
            switch (c)
            {
            case L'"':
            case L'\\':
                n++;
                break;
            case ' ':
            case '\t':
                hasspace = true;
                break;
            default:
                break;
            }
        }
        if (hasspace)
        {
            n += 2;
        }
        if (n == ac.size())
        {
            return std::wstring(ac.data(), ac.size());
        }
        std::wstring buf;
        if (hasspace)
        {
            buf.push_back(L'"');
        }
        size_t slashes = 0;
        for (auto c : ac)
        {
            switch (c)
            {
            case L'\\':
                slashes++;
                buf.push_back(L'\\');
                break;
            case L'"':
            {
                for (; slashes > 0; slashes--)
                {
                    buf.push_back(L'\\');
                }
                buf.push_back(L'\\');
                buf.push_back(c);
            }
            break;
            default:
                slashes = 0;
                buf.push_back(c);
                break;
            }
        }
        if (hasspace)
        {
            for (; slashes > 0; slashes--)
            {
                buf.push_back(L'\\');
            }
            buf.push_back(L'"');
        }
        return buf;
    }
}

#endif
