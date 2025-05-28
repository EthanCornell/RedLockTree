#ifndef UTIL_PRINT_HPP
#define UTIL_PRINT_HPP

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace util
{

namespace detail
{

template <typename T>
std::string to_string(const T& value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

template <typename... Args>
std::string format(const std::string& template_str, Args... args)
{
    std::ostringstream stream;
    std::vector<std::string> arg_list = {to_string(args)...};

    size_t start_pos = 0;
    size_t arg_index = 0;
    while (start_pos < template_str.size())
    {
        size_t open_brace = template_str.find('{', start_pos);
        size_t close_brace = template_str.find('}', open_brace);

        if (open_brace == std::string::npos ||
            close_brace == std::string::npos)
        {
            stream << template_str.substr(start_pos);
            break;
        }

        stream << template_str.substr(start_pos, open_brace - start_pos);

        if (arg_index < arg_list.size())
        {
            stream << arg_list[arg_index++];
        }

        start_pos = close_brace + 1;
    }

    return stream.str();
}

}  // namespace detail

template <typename... Args>
void println(const std::string& message, Args... args)
{
    std::cout << detail::format(message, args...) << std::endl;
}

}  // namespace util

#endif  // UTIL_PRINT_HPP
