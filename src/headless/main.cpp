// SPDX-License-Identifier: MIT
#include "headless_dispatcher.h"
#include "json_output.h"

int main(int argc, char* argv[])
{
    auto args = Headless::parseArgs(argc, argv);
    if (args.interactive)
    {
        if (args.uiOutputDir.empty())
        {
            Headless::writeError("ui_output_dir is required for interactive mode");
            return 1;
        }
        return Headless::runInteractive(args.uiOutputDir);
    }
    return Headless::dispatch(args);
}
