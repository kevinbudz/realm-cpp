#pragma once

namespace realm::application {
// Both launchers execute this coordinator; only the initial mode differs.
int run(int argc, char** argv, bool client_default);
}
