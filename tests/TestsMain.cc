//
// TestsMain.cc
//
// Copyright © 2021 Jens Alfke. All rights reserved.
//

#define CATCH_CONFIG_CONSOLE_WIDTH 120
#define CATCH_CONFIG_RUNNER  // This tells Catch to provide a main() - only do this in one cpp file

#include "catch.hpp"
#include "CaseListReporter.hh"


int main (int argc, char * argv[]) {
    return Catch::Session().run( argc, argv );
}
