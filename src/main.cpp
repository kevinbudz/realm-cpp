#include "realm/application.hpp"
#ifndef REALM_CLIENT_DEFAULT
#define REALM_CLIENT_DEFAULT 0
#endif

int main(int argc, char** argv) {
    return realm::application::run(argc, argv, REALM_CLIENT_DEFAULT != 0);
}
