#pragma once

namespace realm::platform {
// Current resident memory in MiB; zero on platforms without a provider.
double resident_megabytes();
}
