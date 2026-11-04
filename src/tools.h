// Command-line tools (Utils/UtilsManager.cs): -help, -png2font <png>, -docs.
#pragma once

namespace c93 {

// -1: no arguments, start the machine. Otherwise the arguments were handled (as the reference,
// any argument at all ends the run once they are processed) and this is the exit code.
int runTool(int argc, char** argv);

} // namespace c93
