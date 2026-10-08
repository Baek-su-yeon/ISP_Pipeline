#include "options.h"

#include <iostream>

ParseResult ParseOptions(int argc, char* argv[], Options& opt, std::string& input) {
    bool hasInput = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help") return ParseResult::Help;
        else if (arg == "--swap-rb") opt.swapRB = true;
        else if (arg == "--swap-rb-demosaic") opt.swapRBDemosaic = true;
        else if (arg == "--no-black") opt.noBlack = true;
        else if (arg == "--wb-noclip") opt.wbNoclip = true;
        else if (arg == "--quantize-linear") opt.quantizeLinear = true;
        else if (arg == "--compare-libraw") opt.compareLibraw = true;
        else if (arg.rfind("-", 0) == 0) {
            std::cerr << "Unknown option: " << arg << std::endl;
            return ParseResult::Error;
        }
        else {
            if (hasInput) {
                std::cerr << "Only one input file is allowed: " << arg << std::endl;
                return ParseResult::Error;
            }
            input = arg;
            hasInput = true;
        }
    }

    if (!hasInput) {
        std::cerr << "Missing input filename" << std::endl;
        return ParseResult::Error;
    }

    if (opt.swapRB && opt.swapRBDemosaic) {
        std::cerr << "--swap-rb and --swap-rb-demosaic cannot be used together" << std::endl;
        return ParseResult::Error;
    }

    if (opt.compareLibraw && opt.anyExperiment()) {
        std::cerr << "--compare-libraw cannot be used with experiment options" << std::endl;
        return ParseResult::Error;
    }

    return ParseResult::Ok;
}

void PrintUsage(std::ostream& os, const char* prog) {
    os << "Usage: " << prog << " <input.dng> [options...]\n"
       << "Options:\n"
       << "  --swap-rb            treat the Bayer pattern as R<->B swapped in every stage (exp 1-a)\n"
       << "  --swap-rb-demosaic   swap R<->B only in the Demosaic stage (exp 1-b)\n"
       << "                       (cannot be used with --swap-rb)\n"
       << "  --no-black           skip Black Level subtraction, normalize as raw / WL (exp 2)\n"
       << "  --wb-noclip          feed the unclipped WB image to Demosaic (exp 3)\n"
       << "  --quantize-linear    quantize the linear Demosaic result to 8 bit before gamma\n"
       << "  --compare-libraw     compare the normal result with LibRaw reference images (A1, A2, B)\n"
       << "                       (cannot be used with the experiment options above)\n"
       << "  --help               show this message\n"
       << "Results with experiment options go to output/img/<name>/exp_<options>/\n"
       << "Results of --compare-libraw go to output/img/<name>/libraw/" << std::endl;
}

std::vector<std::string> ExperimentOptions(const Options& opt) {
    std::vector<std::string> names;
    if (opt.swapRB) names.push_back("swap-rb");
    if (opt.swapRBDemosaic) names.push_back("swap-rb-demosaic");
    if (opt.noBlack) names.push_back("no-black");
    if (opt.wbNoclip) names.push_back("wb-noclip");
    if (opt.quantizeLinear) names.push_back("quantize-linear");
    return names;
}

std::vector<std::string> EnabledOptions(const Options& opt) {
    std::vector<std::string> names = ExperimentOptions(opt);
    if (opt.compareLibraw) names.push_back("compare-libraw");
    return names;
}

std::string ExpDirName(const Options& opt) {
    std::string name = "exp";
    for (const auto& n : ExperimentOptions(opt)) name += "_" + n;
    return name;
}
