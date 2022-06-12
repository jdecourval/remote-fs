#include <iostream>
#include <span>
#include <string_view>

#include "client.h"

void print_help()
{
}

int main(int argc, char *argv[])
{
    auto args = std::span(argv, argc);

    // Print help if no arguments are given
    if (args.size() == 1)
    {
        print_help();
        return 0;
    }

    // process parameters
    for (auto arg : args)
    {
        auto tmp = std::string_view(arg);

        if (tmp == "--help" || tmp == "-h")
        {
            print_help();
            return 0;
        }
    }
    // process rest of the free arguments. EG. file list, word list
    for (auto arg : args)
    {
        std::cout << arg << std::endl;
    }

    auto l = remotefs::client(argc - 1, argv);
    l.start(args.back());

    return 0;
}
