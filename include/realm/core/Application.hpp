#pragma once

namespace realm::application {
// The realm_client coordinator: the startup flow by default, or a test-only mode selected on the
// command line.
int run(int argc, char** argv);
}
