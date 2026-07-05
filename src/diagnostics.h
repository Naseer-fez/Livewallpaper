#pragma once
#include <windows.h>
#include <string>

namespace Diagnostics {
    // Run a comprehensive machine environment diagnostic and write the report
    // to %APPDATA%\LiveWallpaper\diagnostic_report.txt and stdout.
    // Returns true if the report was successfully generated.
    bool RunDiagnosticReport();
}
